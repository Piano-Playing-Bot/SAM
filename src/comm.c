#define AIL_ALL_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#define AIL_TIME_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "ail_time.h"
#include "common.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <xpsprint.h>

#define CONN_CHECK_TIMEOUT 1000 // in ms

// @Note: Only find_server_port writes this value
static u64 ArduinoPort = 0;
// @Note: Only check_connection_daemon writes this value
static bool IsArduinoConnected = false;

typedef struct ThreadedSendMsgInput {
	bool done;     // Gets set from within threaded_send_msg - indicates whether function completed
	bool succ;     // Gets set from within threaded_send_msg - indicates whether msg was sent successfully
	ClientMsg msg; // Msg to send
} ThreadedSendMsgInput;

// @Note: Function is thread-safe and ensures that only ever one thread is using the Serial interface of the Arduino
bool send_msg(ClientMsg msg) {
	static pthread_mutex_t ArduinoPortMutex = PTHREAD_MUTEX_INITIALIZER;
	u8 msgBuffer[MAX_CLIENT_MSG_SIZE] = {0};
	AIL_Buffer buffer = {
		.data = msgBuffer,
		.idx  = 0,
		.len  = 0,
		.cap  = MAX_CLIENT_MSG_SIZE,
	};
	ail_buf_write4msb(&buffer, SPPP_MAGIC);
	ail_buf_write4msb(&buffer, msg.type);
	switch (msg.type) {
		case MSG_PIDI:
			ail_buf_write4lsb(&buffer, msg.n * ENCODED_MUSIC_CHUNK_LEN);
			for (u64 i = 0; i < msg.n; i++) {
				encode_chunk(&buffer, msg.chunks[i]);
			}
			break;
		case MSG_JUMP:
			ail_buf_write4lsb(&buffer, 8);
			ail_buf_write8lsb(&buffer, msg.n);
			break;
		default:
			ail_buf_write4lsb(&buffer, 0x20202020);
			break;
	}
	printf("Sending message (len=%lld): '%s'\n", buffer.len, (char *)buffer.data); // @Cleanup

	while (pthread_mutex_lock(&ArduinoPortMutex) != 0) ail_time_sleep(50);
	printf("Writing...\n"); // @Nocheckin
	if (!ail_fs_write_n_bytes(ArduinoPort, (char *)buffer.data, buffer.len)) return false;
	u64 len;
	u8 reply[MAX_SERVER_MSG_SIZE];
	printf("Reading...\n"); // @Nocheckin
	if (!ail_fs_read_n_bytes(ArduinoPort, reply, MAX_SERVER_MSG_SIZE, &len)) return false;
	printf("Received reply: '%s'\n", reply); // @Cleanup

	while (pthread_mutex_unlock(&ArduinoPortMutex) != 0) {}


	if (len < 8) return false;
	if (msg.type == MSG_PING) {
		return memcmp(reply, "SPPPPONG", 8) == 0;
	} else {
		return memcmp(reply, "SPPPSUCC", 8) == 0;
	}
}

// Send message but for calling in a seperate thread
// Arg should be a pointer to a ThreadedSendMsgInput structure
void *threaded_send_msg(void *arg)
{
	ThreadedSendMsgInput *input = arg;
	input->succ = send_msg(input->msg);
	input->done = true;
	return NULL;
}

// @Note: Updates ArduinoPort
void find_server_port(AIL_Allocator *allocator)
{
    ClientMsg ping = { .type = MSG_PING };
	if (ArduinoPort) {
		if (send_msg(ping)) return;
		ail_fs_close_file(ArduinoPort);
		ArduinoPort = 0;
	}

	unsigned long ports_amount = 0;
    unsigned long required_size;
    bool res = EnumPorts(NULL, 2, NULL, 0, &required_size, &ports_amount);
    PORT_INFO_2 *ports = allocator->alloc(allocator->data, required_size);
	AIL_ASSERT(ports != NULL);
    res = EnumPorts(NULL, 2, (u8 *)ports, required_size, &required_size, &ports_amount);
    if (res == 0) {
        AIL_DBG_PRINT("Error in enumerating ports: %ld\n", GetLastError());
        return;
    }

    for (unsigned long i = 0; i < ports_amount; i++) {
        PORT_INFO_2 port = ports[i];
		bool port_is_rw  = port.fPortType & PORT_TYPE_READ && port.fPortType & PORT_TYPE_WRITE;
		bool port_is_usb = strlen(port.pPortName) >= 3 && memcmp(port.pPortName, "COM", 3) == 0;
        if (port_is_rw && port_is_usb) {
            if (ail_fs_open_file(port.pPortName, &ArduinoPort, true)) {
                if (send_msg(ping)) goto done;
                ail_fs_close_file(ArduinoPort);
            }
        }
    }
	ArduinoPort = 0;
done:
    allocator->free_one(allocator->data, ports);
}

void *check_connection_daemon(void *allocator)
{
	AIL_Allocator *al = allocator;
	while (true) {
		find_server_port(al);
		IsArduinoConnected = ArduinoPort != 0;
		ail_time_sleep(CONN_CHECK_TIMEOUT);
	}
	return NULL;
}
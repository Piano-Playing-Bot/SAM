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
static void *ArduinoPort = 0;
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
	u8 reply[MAX_SERVER_MSG_SIZE]     = {0};
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


	while (pthread_mutex_lock(&ArduinoPortMutex) != 0) ail_time_sleep(50);

	COMMTIMEOUTS timeouts = {
		.ReadIntervalTimeout         = 50,
		.ReadTotalTimeoutConstant    = 50,
		.ReadTotalTimeoutMultiplier  = 10,
		.WriteTotalTimeoutConstant   = 50,
		.WriteTotalTimeoutMultiplier = 10,
	};
	if (!SetCommTimeouts(ArduinoPort, &timeouts)) goto done;
	if (!SetCommMask(ArduinoPort, EV_RXCHAR)) goto done;
	DCB dcb = {
		.DCBlength    = sizeof(DCB),
		.BaudRate     = BAUD_RATE,
		.StopBits     = ONESTOPBIT,
		.Parity       = (BYTE)PARITY_NONE,
		.fOutxCtsFlow = false,
    	.fRtsControl  = RTS_CONTROL_DISABLE,
    	.fOutX        = false,
    	.fInX         = false,
		.EofChar      = EOF,
		.ByteSize     = 8,
	};
	if (!SetupComm(ArduinoPort, 4096, 4096)) goto done;
	if (!SetCommState(ArduinoPort, &dcb)) goto done;
	DWORD written;
	if (!WriteFile(ArduinoPort, buffer.data, buffer.len, &written, 0)) goto done;
	DWORD read;
	if (!ReadFile(ArduinoPort, reply, MAX_SERVER_MSG_SIZE, &read, 0)) goto done;

done:
	while (pthread_mutex_unlock(&ArduinoPortMutex) != 0) {}


	if (read < 8) return false;
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
		CloseHandle(ArduinoPort);
		ArduinoPort = NULL;
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
			ArduinoPort = CreateFile(port.pPortName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
			if (ArduinoPort != INVALID_HANDLE_VALUE) {
				printf("Checking port '%s'...\n", port.pPortName);
                if (send_msg(ping)) goto done;
                CloseHandle(ArduinoPort);
			}
        }
    }
	ArduinoPort = NULL;
done:
    allocator->free_one(allocator->data, ports);
}

void *check_connection_daemon(void *allocator)
{
	AIL_Allocator *al = allocator;
	while (true) {
		find_server_port(al);
		IsArduinoConnected = ArduinoPort != NULL;
		ail_time_sleep(CONN_CHECK_TIMEOUT);
	}
	return NULL;
}
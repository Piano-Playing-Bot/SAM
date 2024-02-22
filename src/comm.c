#define AIL_ALL_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#define AIL_TIME_IMPL
#define AIL_ALLOC_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "ail_time.h"
#include "ail_alloc.h"
#include "common.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <xpsprint.h>

#define CONN_CHECK_TIMEOUT 1.0f // in seconds
#define NEXT_MSGS_COUNT 4
#define SEND_MSG_MAX_RETRIES 8

typedef struct NextMsgRing {
    ClientMsgType data[NEXT_MSGS_COUNT];
    u8 start;
    u8 end;
} NextMsgRing;
AIL_STATIC_ASSERT(NEXT_MSGS_COUNT <= UINT8_MAX);

// @Note: All communication with the Arduino is done in a single thread external from the UI's main thread.
// No other thread should write to these variables
static void *comm_port             = 0; // Handle to the Port that is connected to the Arduino - Only find_server_port writes this value
static bool  comm_is_music_playing = false;
static bool  comm_is_connected     = false;
static f32   comm_volume           = 1.0f;
static f32   comm_speed            = 1.0f;
static f32   comm_time             = 1.0f;
static u32   comm_pidi_chunk_idx   = 0;
static u32   comm_cmds_idx         = 0;
static AIL_DA(PidiCmd) comm_cmds   = { 0 };
static NextMsgRing comm_next_msgs  = { 0 };
static f64   last_comm_time        = 0.0f; // Timestamp of last received message from Arduino - Only read_msg_fast write this value
static u8 comm_piano[KEYS_AMOUNT];

static pthread_mutex_t comm_volume_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comm_speed_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comm_song_mutex   = PTHREAD_MUTEX_INITIALIZER;

// For writing to the communication thread, the main thread should call the following functions
void send_new_song(AIL_DA(PidiCmd) cmds, f32 start_time);
void set_volume(f32 volume);
void set_speed(f32 speed);

// Internal only functions
bool comm_setup_port(void);
bool send_msg(ClientMsg msg, bool retry);
ServerMsgType read_msg(bool retry);
ServerMsgType read_msg_fast(bool retry);
void find_server_port(AIL_Allocator *allocator);
static inline void push_msg(ClientMsgType msg);
static inline ClientMsgType pop_msg(void);
static inline bool next_msgs_contain_pidi(void);


// Main loop for Communication Thread
void *comm_thread_main(void *args)
{
    AIL_UNUSED(args);
    AIL_Allocator arena = ail_alloc_arena_new(2*AIL_ALLOC_PAGE_SIZE, &ail_alloc_pager);
    AIL_ASSERT(arena.data != NULL); // @TODO: Show error message if something goes wrong
    while (true) {
        // If we are not connected, find port to connect
        if (!comm_is_connected && ail_time_clock_elapsed(last_comm_time) >= CONN_CHECK_TIMEOUT) {
            find_server_port(&arena);
            comm_is_connected = comm_port != NULL;
        }
        // Check incoming messages from server
        if (comm_is_connected && read_msg(false) == SMSG_REQP) {
            while (pthread_mutex_lock(&comm_song_mutex) != 0) {}
            if (comm_cmds_idx < comm_cmds.len) {

            } else {
                ClientMsg msg = {
                    .type = CMSG_PIDI,
                    .data = { .pidi = { 0 } },
                };
                send_msg(msg, true);
            }
            while (pthread_mutex_unlock(&comm_song_mutex) != 0) {}
        }
        // Send any queued up messages
        ClientMsgType next_msg;
        while (comm_is_connected && (next_msg = pop_msg())) {
            ClientMsg msg;
            switch (next_msg) {
                case CMSG_NONE:
                    AIL_UNREACHABLE();
                    goto skip_sending_message;
                case CMSG_PING:
                case CMSG_CONT:
                case CMSG_STOP:
                    msg = (ClientMsg) {
                        .type = next_msg,
                        .data = { 0 },
                    };
                    break;
                case CMSG_LOUD:
                    msg = (ClientMsg) {
                        .type = CMSG_LOUD,
                        .data = { .f = comm_volume },
                    };
                    break;
                case CMSG_SPED:
                    msg = (ClientMsg) {
                        .type = CMSG_SPED,
                        .data = { .f = comm_speed },
                    };
                    break;
                case CMSG_PIDI:
                    if (next_msgs_contain_pidi()) goto skip_sending_message;
                    memset(comm_piano, 0, KEYS_AMOUNT);
                    u8 active_keys_count = 0;
                    for (u32 i = 0; i < comm_cmds.len && comm_cmds.data[i].time <= comm_time; i++) {
                        apply_pidi_cmd(comm_piano, comm_cmds.data, i, comm_cmds.len, &active_keys_count);
                    }
                    ClientMsgPidiData pidi = {
                        .time       = comm_time,
                        .cmds_count = comm_cmds.len,
                        .cmds       = comm_cmds.data,
                        .idx        = 0,
                        .piano      = comm_piano,
                    };
                    msg = (ClientMsg) {
                        .type = CMSG_PIDI,
                        .data = { .pidi = pidi },
                    };
                    break;
            }
            if (!send_msg(msg, true)) comm_is_connected = false;
            else if (next_msg == CMSG_PIDI) comm_is_music_playing = true;
            else if (next_msg == CMSG_CONT || next_msg == CMSG_STOP) comm_is_music_playing = !comm_is_music_playing;
skip_sending_message:
            AIL_UNUSED(0);
        }
    }
    if (comm_port) CloseHandle(comm_port);
    return NULL;
}

void push_msg(ClientMsgType msg)
{
    comm_next_msgs.data[comm_next_msgs.end] = msg;
    comm_next_msgs.end = (comm_next_msgs.end + 1)%NEXT_MSGS_COUNT;
}

ClientMsgType pop_msg(void)
{
    u8 idx = comm_next_msgs.start;
    if (idx == comm_next_msgs.end) return CMSG_NONE;
    else {
        comm_next_msgs.start = (comm_next_msgs.start + 1)%NEXT_MSGS_COUNT;
        return comm_next_msgs.data[idx];
    }
}

// Checks whether comm_next_msgs contains any CMSG_PIDI
bool next_msgs_contain_pidi(void)
{
    u8 n = (comm_next_msgs.end < comm_next_msgs.start)*NEXT_MSGS_COUNT + comm_next_msgs.end - comm_next_msgs.start;
    for (u8 i = 0, j = 0; i < n; i++, j = (j + 1)%NEXT_MSGS_COUNT) {
        if (comm_next_msgs.data[j] == CMSG_PIDI) return true;
    }
    return false;
}

void send_new_song(AIL_DA(PidiCmd) cmds, f32 start_time)
{
    while (pthread_mutex_lock(&comm_song_mutex) != 0) {}
    printf("\033[33mSENDING NEW SONG at time %f\033[0m\n", start_time);
    if (comm_cmds.data) ail_da_free(&comm_cmds);
    comm_pidi_chunk_idx = 0;
    comm_time = start_time;
    comm_cmds = cmds;
    push_msg(CMSG_PIDI);
    while (pthread_mutex_unlock(&comm_song_mutex) != 0) {}
}

void set_paused(bool paused)
{
    while (pthread_mutex_lock(&comm_volume_mutex) != 0) {}
    if (paused) push_msg(CMSG_STOP);
    else        push_msg(CMSG_CONT);
    while (pthread_mutex_unlock(&comm_volume_mutex) != 0) {}
}

void set_volume(f32 volume)
{
    while (pthread_mutex_lock(&comm_volume_mutex) != 0) {}
    comm_volume = volume;
    push_msg(CMSG_LOUD);
    while (pthread_mutex_unlock(&comm_volume_mutex) != 0) {}
}

void set_speed(f32 speed)
{
    while (pthread_mutex_lock(&comm_speed_mutex) != 0) {}
    comm_speed = speed;
    push_msg(CMSG_SPED);
    while (pthread_mutex_unlock(&comm_speed_mutex) != 0) {}
}

bool comm_setup_port(void) {
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = 50,
        .ReadTotalTimeoutConstant    = 50,
        .ReadTotalTimeoutMultiplier  = 10,
        .WriteTotalTimeoutConstant   = 50,
        .WriteTotalTimeoutMultiplier = 10,
    };
    if (!SetCommTimeouts(comm_port, &timeouts)) return false;
    if (!SetCommMask(comm_port, EV_RXCHAR)) return false;
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
    if (!SetupComm(comm_port, 4096, 4096)) return false;
    if (!SetCommState(comm_port, &dcb)) return false;
    return true;
}

ServerMsgType read_msg_fast(bool retry)
{
    u8 msg[MAX_SERVER_MSG_SIZE] = {0};
    bool res;
    u32 attempts = 0;
    DWORD read;
    while (!(res = ReadFile(comm_port, msg, MAX_SERVER_MSG_SIZE, &read, 0)) && retry && attempts++ < SEND_MSG_MAX_RETRIES) {}
    if (!res) return SMSG_NONE;

    // printf("Read message (len=%lu): '%s'\n", read, msg);
    if (read < 8) return SMSG_NONE;
    u32 magic = (((u32)msg[0]) << 24) | (((u32)msg[1]) << 16) | (((u32)msg[2]) << 8) | (((u32)msg[3]) << 0);
    u32 type  = (((u32)msg[4]) << 24) | (((u32)msg[5]) << 16) | (((u32)msg[6]) << 8) | (((u32)msg[7]) << 0);
    if (magic != SPPP_MAGIC) return SMSG_NONE;
    last_comm_time = ail_time_clock_start();
    printf("Read message of type '%s'\n", (char *)msg);
    return type;
}

ServerMsgType read_msg(bool retry)
{
    if (!comm_setup_port()) return SMSG_NONE;
    return read_msg_fast(retry);
}

// @Note: Function is thread-safe and ensures that only ever one thread is using the Serial interface of the Arduino
bool send_msg(ClientMsg msg, bool retry)
{
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
        case CMSG_PIDI: {
            ClientMsgPidiData pidi = msg.data.pidi;
            if (pidi.idx == 0) {
                ail_buf_write4lsb(&buffer, 4 + 8 + KEYS_AMOUNT + pidi.cmds_count * ENCODED_MUSIC_CHUNK_LEN);
                ail_buf_write4lsb(&buffer, pidi.idx);
                ail_buf_write8lsb(&buffer, pidi.time);
                memcpy(&buffer.data[buffer.idx], pidi.piano, KEYS_AMOUNT);
                buffer.idx += KEYS_AMOUNT;
                buffer.len += KEYS_AMOUNT;
                for (u32 i = 0; i < pidi.cmds_count; i++) {
                    encode_cmd(&buffer, pidi.cmds[i]);
                }
            } else {
                ail_buf_write4lsb(&buffer, 4 + pidi.cmds_count * ENCODED_MUSIC_CHUNK_LEN);
                ail_buf_write4lsb(&buffer, pidi.idx);
                for (u32 i = 0; i < pidi.cmds_count; i++) {
                    encode_cmd(&buffer, pidi.cmds[i]);
                }
            }
        } break;
        case CMSG_LOUD:
        case CMSG_SPED:
            ail_buf_write4lsb(&buffer, 4);
            ail_buf_write4lsb(&buffer, *(u32 *)&msg.data.f);
            break;
        default:
            ail_buf_write4lsb(&buffer, 0);
            break;
    }

    if (!comm_setup_port()) return false;
    printf("Writing message '");
    for (u8 i = 0; i < 8; i++) printf("%c", buffer.data[i]);
    for (u8 i = 8; i < buffer.len; i++) printf(" %u", buffer.data[i]);
    printf("'\n");
    u8 attempts = 0;
    bool res;
    DWORD written;
    while (!(res = WriteFile(comm_port, buffer.data, buffer.len, &written, 0)) && retry && attempts++ < SEND_MSG_MAX_RETRIES) {}
    if (!res) return false;

    ServerMsgType reply = read_msg_fast(retry);
    if (msg.type == CMSG_PING) {
        return reply == SMSG_PONG;
    } else {
        return reply == SMSG_SUCC;
    }
}

// @Note: Updates comm_port
void find_server_port(AIL_Allocator *allocator)
{
    ClientMsg ping = { .type = CMSG_PING };
    if (comm_port) {
        if (send_msg(ping, false)) return;
        CloseHandle(comm_port);
        comm_port = NULL;
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
            comm_port = CreateFile(port.pPortName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
            if (comm_port != INVALID_HANDLE_VALUE) {
                // printf("Checking port '%s'...\n", port.pPortName);
                if (send_msg(ping, false)) goto done;
                CloseHandle(comm_port);
            }
        }
    }
    comm_port = NULL;
done:
    allocator->free_one(allocator->data, ports);
}
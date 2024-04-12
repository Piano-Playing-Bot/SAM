#include "header.h"
#include <windows.h>
#include <xpsprint.h>

#define NEXT_MSGS_COUNT 4
#define SEND_MSG_MAX_RETRIES 8
#define MAX_BYTES_TO_SEND_AT_ONCE 16
#define READING_CHUNK_SIZE 8
#define MAX_CLIENT_MSG_SIZE (4 + 1 + 3*12*(1<<4) + 2 + 4*(1<<16))

typedef struct NextMsgRing {
    ClientMsgType data[NEXT_MSGS_COUNT];
    u8 start;
    u8 end;
} NextMsgRing;
AIL_STATIC_ASSERT(NEXT_MSGS_COUNT <= UINT8_MAX);
AIL_DA_INIT(PlayedKeySPPP);

// @Note: All communication with the Arduino is done in a single thread external from the UI's main thread.
// No other thread should write to these variables
static void *comm_port             = 0;    // Handle to the Port that is connected to the Arduino - Only find_server_port writes this value
static bool  comm_is_music_playing = false;
static bool  comm_is_paused        = false;
static bool  comm_is_connected     = false;
static f32   comm_volume           = 1.0f;
static f32   comm_speed            = 1.0f;
static u32   comm_time             = 0;
static u32   comm_pidi_chunk_idx   = 0;
static u32   comm_cmds_idx         = 0;
static u16   comm_max_cmds_per_msg = 0;
static bool  comm_ignore_requests  = false; // Indicates whether to ignore REQP messages for this loop iteration, because we just sent a new PIDI message
static f64   last_comm_time        = 0.0f;  // Timestamp of last received message from Arduino - Only read_msg_fast write this value
static ClientMsg comm_last_sent    = { 0 }; // Last message that was sent to the Arduino
static AIL_RingBuffer comm_rb      = { 0 };
static AIL_DA(PidiCmd) comm_cmds   = { 0 };
static NextMsgRing comm_next_msgs  = { 0 };
static AIL_DA(PlayedKeySPPP) comm_played_keys = { 0 };

static pthread_mutex_t comm_volume_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comm_speed_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comm_song_mutex   = PTHREAD_MUTEX_INITIALIZER;

// For writing to the communication thread, the main thread should call the following functions
void send_new_song(AIL_DA(PidiCmd) cmds, u32 start_time);
void set_volume(f32 volume);
void set_speed(f32 speed);

// Internal only functions
bool comm_setup_port(void);
bool send_msg(ClientMsg msg);
void find_server_port(AIL_Allocator *allocator);
static inline void push_msg(ClientMsgType msg);
static inline ClientMsgType pop_msg(void);
static inline bool next_msgs_contain_pidi(void);
static inline void listen_to_port(void);
ServerMsgType check_for_msg(void);


// @TODO: have a timer, that tells the UI the current time

// Main loop for Communication Thread
void *comm_thread_main(void *args)
{
    AIL_UNUSED(args);
    comm_played_keys = ail_da_new_with_cap(PlayedKeySPPP, PIANO_KEY_AMOUNT*(1<<4));
    AIL_Allocator arena = ail_alloc_arena_new(2*AIL_ALLOC_PAGE_SIZE, &ail_alloc_pager);
    AIL_ASSERT(arena.data != NULL); // @TODO: Show error message if something goes wrong
    while (true) {
        comm_ignore_requests = false;
        // If we are not connected, find port to connect
        if (ail_time_clock_elapsed(last_comm_time) >= MSG_TIMEOUT/1000.0f) {
            if (!comm_is_connected) {
                find_server_port(&arena);
                comm_is_connected = comm_port != NULL;
            } else if (comm_last_sent.type != CMSG_NONE) {
                // @Cleanup:
                // comm_last_sent.type = CMSG_NONE;
                printf("Sending msg again\n");
                send_msg(comm_last_sent); // Send same message again, since something apparently went wrong
                // @TODO: Potential problem here:
                // UI sends PIDI chunk
                // Arduino receives it, but SPPPSUCC message is lost on way
                // UI sends same PIDI chunk again
                // the same music is played twice
            }
        }

        // Send any queued up messages
        ClientMsgType next_msg;
        while (comm_is_connected && comm_last_sent.type == CMSG_NONE && (next_msg = pop_msg())) {
            ClientMsg msg;
            switch (next_msg) {
                case CMSG_NONE:
                    AIL_UNREACHABLE();
                    goto skip_sending_message;
                case CMSG_PING:
                case CMSG_CONTINUE:
                    msg = (ClientMsg) {
                        .type = next_msg,
                        .data = { .b = !comm_is_paused },
                    };
                    break;
                case CMSG_VOLUME:
                    msg = (ClientMsg) {
                        .type = CMSG_VOLUME,
                        .data = { .f = comm_volume },
                    };
                    break;
                case CMSG_SPEED:
                    msg = (ClientMsg) {
                        .type = CMSG_SPEED,
                        .data = { .f = comm_speed },
                    };
                    break;
                case CMSG_NEW_MUSIC:
                    comm_ignore_requests = true;
                    if (next_msgs_contain_pidi()) goto skip_sending_message;
                    u32 i = 0;
                    u32 prev_cmd_time = 0;
                    for (; i < comm_cmds.len && prev_cmd_time + comm_cmds.data[i].dt < comm_time; i++) {
                        PidiCmd cmd  = comm_cmds.data[i];
                        u32 end_time = prev_cmd_time + cmd.dt + cmd.len*LEN_FACTOR;
                        if (comm_time < end_time) {
                            PlayedKeySPPP pk = {
                                .key      = cmd.key,
                                .octave   = cmd.octave,
                                .len      = (end_time - comm_time)/LEN_FACTOR,
                                .velocity = cmd.velocity,
                            };
                            ail_da_push(&comm_played_keys, pk);
                        }
                        prev_cmd_time += cmd.dt;
                    }
                    ClientMsgPidiData pidi = {
                        .pks_count   = comm_played_keys.len,
                        .played_keys = comm_played_keys.data,
                        .cmds_count  = AIL_MIN(comm_cmds.len - i, comm_max_cmds_per_msg),
                        .cmds        = &comm_cmds.data[i],
                    };
                    comm_cmds_idx = i + pidi.cmds_count;
                    msg = (ClientMsg) {
                        .type = CMSG_MUSIC,
                        .data = { .pidi = pidi },
                    };
                    break;
                case CMSG_MUSIC:
                    AIL_UNREACHABLE();
                    break;
            }
            comm_is_connected = send_msg(msg);
skip_sending_message:
            AIL_UNUSED(0); // to allow the label `skip_sending_message` to exist here
        }

        // Read data from port into ring buffer
        if (comm_is_connected) {
            listen_to_port();
            ServerMsgType res;
            do {
                res = check_for_msg();
                switch (res) {
                    case SMSG_PONG:
                    case SMSG_SUCCESS:
                        switch (comm_last_sent.type) {
                            case CMSG_NEW_MUSIC:
                            case CMSG_MUSIC:
                                comm_is_music_playing = true;
                                break;
                            default:
                                break;
                        }
                        comm_last_sent = (ClientMsg){0}; // indicates, that last message was received successfully by arduino
                        break;
                    case SMSG_REQUEST: {
                        if (comm_ignore_requests) continue;
                        while (pthread_mutex_lock(&comm_song_mutex) != 0) {}
                        ClientMsg msg = { .type = CMSG_MUSIC };
                        if (comm_cmds_idx < comm_cmds.len) {
                            msg.data.pidi = (ClientMsgPidiData) {
                                .pks_count   = 0,
                                .played_keys = comm_played_keys.data,
                                .cmds_count  = AIL_MIN(comm_cmds.len - comm_cmds_idx, comm_max_cmds_per_msg),
                                .cmds        = &comm_cmds.data[comm_cmds_idx],
                            };
                            comm_cmds_idx += comm_max_cmds_per_msg;
                        }
                        else {
                            msg.data.pidi = (ClientMsgPidiData) {
                                .pks_count   = 0,
                                .played_keys = comm_played_keys.data,
                                .cmds_count  = 0,
                                .cmds        = 0,
                            };
                        }
                        send_msg(msg);
                        while (pthread_mutex_unlock(&comm_song_mutex) != 0) {}
                    } break;
                    case SMSG_NONE: {}
                }
            } while (res != SMSG_NONE);
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

// Checks whether comm_next_msgs contains any CMSG_MUSIC or CMSG_NEW_MUSIC
bool next_msgs_contain_pidi(void)
{
    u8 n = (comm_next_msgs.end < comm_next_msgs.start)*NEXT_MSGS_COUNT + comm_next_msgs.end - comm_next_msgs.start;
    for (u8 i = 0, j = 0; i < n; i++, j = (j + 1)%NEXT_MSGS_COUNT) {
        if (comm_next_msgs.data[j] == CMSG_MUSIC || comm_next_msgs.data[j] == CMSG_NEW_MUSIC) return true;
    }
    return false;
}

void send_new_song(AIL_DA(PidiCmd) cmds, u32 start_time)
{
    while (pthread_mutex_lock(&comm_song_mutex) != 0) {}
    // printf("\033[33mSENDING NEW SONG at time %f\033[0m\n", start_time);
    if (comm_cmds.data) ail_da_free(&comm_cmds);
    comm_pidi_chunk_idx = 0;
    comm_time = start_time;
    comm_cmds = cmds;
    push_msg(CMSG_NEW_MUSIC);
    while (pthread_mutex_unlock(&comm_song_mutex) != 0) {}
}

void set_paused(bool paused)
{
    while (pthread_mutex_lock(&comm_volume_mutex) != 0) {}
    comm_is_paused = paused;
    push_msg(CMSG_CONTINUE);
    while (pthread_mutex_unlock(&comm_volume_mutex) != 0) {}
}

void set_volume(f32 volume)
{
    while (pthread_mutex_lock(&comm_volume_mutex) != 0) {}
    comm_volume = volume;
    push_msg(CMSG_VOLUME);
    while (pthread_mutex_unlock(&comm_volume_mutex) != 0) {}
}

void set_speed(f32 speed)
{
    while (pthread_mutex_lock(&comm_speed_mutex) != 0) {}
    comm_speed = speed;
    push_msg(CMSG_SPEED);
    while (pthread_mutex_unlock(&comm_speed_mutex) != 0) {}
}

void close_comm(void)
{
    if (comm_port) {
        send_msg((ClientMsg) {
            .type = CMSG_CONTINUE,
            .data = { .b = false },
        });
        CloseHandle(comm_port);
        comm_port = NULL;
        comm_is_connected = false;
    }
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
        .DCBlength       = sizeof(DCB),
        .BaudRate        = BAUD_RATE,
        .StopBits        = ONESTOPBIT,
        .Parity          = (BYTE)PARITY_NONE,
        .fOutX           = false,
        .fInX            = false,
        .EofChar         = EOF,
        .ByteSize        = 8,
        .fDtrControl     = DTR_CONTROL_DISABLE,
        .fRtsControl     = RTS_CONTROL_DISABLE,
        .fOutxCtsFlow    = 0,
        .fOutxDsrFlow    = 0,
        .fDsrSensitivity = 0,
    };
    if (!SetupComm(comm_port, 4096, 4096)) return false;
    if (!SetCommState(comm_port, &dcb)) return false;
    return true;
}

// Read all incoming data from the port into the Ring Buffer
void listen_to_port(void)
{
AIL_STATIC_ASSERT(READING_CHUNK_SIZE < AIL_RING_SIZE/2);
    u8 msg[READING_CHUNK_SIZE] = {0};
    DWORD read;
    bool res = ReadFile(comm_port, msg, READING_CHUNK_SIZE, &read, 0);
    if (comm_is_connected) comm_is_connected = res;
    ail_ring_writen(&comm_rb, (u8)read, msg);
    for (u8 i = 0; i < read; i++) printf("%c", msg[i]);
    // for (u8 i = 0; i < read; i++) printf("Read: %2x\n", msg[i]);
}

// Checks the Ring Buffer for any SPPP messages
ServerMsgType check_for_msg(void)
{
    // Go through Ring Buffer to check if any messages were received
    while (ail_ring_len(comm_rb) >= 4 && (ail_ring_peek4msb(comm_rb) & 0xffffff00) != SPPP_MAGIC) {
        // printf("Popping off: '%c' (%d)\n", (char)ail_ring_peek(comm_rb), (int)ail_ring_peek(comm_rb))
        ail_ring_pop(&comm_rb);
    }
    if (ail_ring_peek_at(comm_rb, 3) == SMSG_PONG) {
        if (ail_ring_len(comm_rb) >= 6) {
            ail_ring_popn(&comm_rb, 4); // Remove magic bytes & type
            comm_max_cmds_per_msg = ail_ring_read2lsb(&comm_rb);
        }
        return SMSG_PONG;
    } else if (ail_ring_len(comm_rb) >= 4) {
        ail_ring_popn(&comm_rb, 3); // Remove magic bytes
        return (ServerMsgType)ail_ring_read(&comm_rb);
    }
    return SMSG_NONE;
}

// Blocks for at most MSG_TIMEOUT until a SPPP message was read from the Port or returns SMSG_NONE otherwise
// Does not check whether comm_is_connected is true
ServerMsgType wait_for_reply(void)
{
    f64 t = ail_time_clock_start();
    while (ail_time_clock_elapsed(t) < (f64)MSG_TIMEOUT/1000.0f) {
        listen_to_port();
        ServerMsgType res = check_for_msg();
        if (res != SMSG_NONE) {
            return res;

        }
    }
    return SMSG_NONE;
}

bool send_msg(ClientMsg msg)
{
#if 1
    char *msg_str;
    switch (msg.type) {
        case CMSG_NONE:      msg_str = "NONE";      break;
        case CMSG_PING:      msg_str = "PING";      break;
        case CMSG_MUSIC:     msg_str = "MUSIC";     break;
        case CMSG_NEW_MUSIC: msg_str = "NEW_MUSIC"; break;
        case CMSG_CONTINUE:  msg_str = "CONTINUE";  break;
        case CMSG_VOLUME:    msg_str = "VOLUME";    break;
        case CMSG_SPEED:     msg_str = "SPEED";     break;
    }
    printf("Sending message of type %s to Arduino\n", msg_str);
#endif
    static u8 msgBuffer[MAX_CLIENT_MSG_SIZE] = {0};
    AIL_Buffer buffer = {
        .data = msgBuffer,
        .idx  = 0,
        .len  = 0,
        .cap  = MAX_CLIENT_MSG_SIZE,
    };
    ail_buf_write4msb(&buffer, SPPP_MAGIC | msg.type);
    switch (msg.type) {
        case CMSG_NEW_MUSIC:
            ail_buf_write1(&buffer, msg.data.pidi.pks_count);
            encode_played_keys(msg.data.pidi.played_keys, msg.data.pidi.pks_count, buffer.data);
            buffer.idx += SPPP_PK_ENCODED_SIZE*msg.data.pidi.pks_count;
            AIL_FALL_THROUGH();
        case CMSG_MUSIC:
            printf("Music with %d commands\n", msg.data.pidi.cmds_count);
            ail_buf_write2lsb(&buffer, msg.data.pidi.cmds_count);
            for (u32 i = 0; i < msg.data.pidi.cmds_count; i++) {
                encode_cmd(&buffer, msg.data.pidi.cmds[i]);
            }
            break;
        case CMSG_CONTINUE:
            ail_buf_write1(&buffer, (u8)msg.data.b);
            break;
        case CMSG_VOLUME:
        case CMSG_SPEED:
            ail_buf_write4lsb(&buffer, *(u32 *)&msg.data.f);
            break;
        default:
            ail_buf_write4lsb(&buffer, 0);
            break;
    }

    // @Cleanup
    // printf("Writing message '");
    // for (u8 i = 4; i < 8; i++) printf("%c", buffer.data[i]);
    // printf("' (%lld bytes)...\n", buffer.len);
    static int i = 0;
    if (msg.type == CMSG_MUSIC || msg.type == CMSG_NEW_MUSIC) {
        char buf[16];
        sprintf(buf, "msg-%d.buf", i++);
        ail_fs_write_file(buf, (char*)msgBuffer, buffer.len);
    }
    // printf("Writing message '");
    // for (u8 i = 0; i < 8; i++) printf("%c", buffer.data[i]);
    // for (u8 i = 8; i < buffer.len; i++) printf(" %u", buffer.data[i]);
    // printf("'\n");

    DWORD written;
    u64 buffer_idx = 0;
    u32 toWrite    = 1; // Set to 1, to enter the loop at least once
    while (true) {
        toWrite = AIL_MIN(buffer.len - buffer_idx, MAX_BYTES_TO_SEND_AT_ONCE);
        if (!toWrite) break;
        // printf("Sending %d bytes...\n", toWrite);
        bool res = WriteFile(comm_port, &buffer.data[buffer_idx], toWrite, &written, 0);
        AIL_ASSERT(res);
        AIL_ASSERT(written == toWrite);
        buffer_idx += toWrite;
        if (buffer_idx < buffer.len) ail_time_sleep(50);
    }
    comm_last_sent = msg;
    last_comm_time = ail_time_clock_start();
    return true;
}

// @Note: Updates comm_port
void find_server_port(AIL_Allocator *allocator)
{
    ClientMsg ping = { .type = CMSG_PING };
    if (comm_port) {
        if (send_msg(ping) && wait_for_reply() == SMSG_PONG) {
            comm_last_sent = (ClientMsg){0};
            return;
        }
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
        goto done;
    }

    for (unsigned long i = 0; i < ports_amount; i++) {
        PORT_INFO_2 port = ports[i];
        bool port_is_rw  = port.fPortType & PORT_TYPE_READ && port.fPortType & PORT_TYPE_WRITE;
        bool port_is_usb = strlen(port.pPortName) >= 3 && memcmp(port.pPortName, "COM", 3) == 0;
        if (port_is_rw && port_is_usb) {
            comm_port = CreateFile(port.pPortName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_SHARE_READ|FILE_SHARE_WRITE, 0);
            if (comm_port != INVALID_HANDLE_VALUE && comm_setup_port()) {
                // printf("Checking port '%s'...\n", port.pPortName);
                if (send_msg(ping) && wait_for_reply() == SMSG_PONG) {
                    comm_last_sent = (ClientMsg){0};
                    goto done;
                }
                CloseHandle(comm_port);
            }
        }
    }
    comm_port = NULL;
done:
    allocator->free_one(allocator->data, ports);
}
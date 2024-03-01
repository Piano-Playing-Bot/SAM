#define AIL_ALL_IMPL
#define AIL_ALLOC_IMPL
#define AIL_RING_IMPL
#define AIL_TIME_IMPL
#include "ail_alloc.h"
#include "ail_ring.h"
#include "ail_time.h"
#include "common.h"
#include "comm.c"

#define PORT_NAME "COM5"

bool setup_port(void *handle) {
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = 50,
        .ReadTotalTimeoutConstant    = 50,
        .ReadTotalTimeoutMultiplier  = 10,
        .WriteTotalTimeoutConstant   = 50,
        .WriteTotalTimeoutMultiplier = 10,
    };
    if (!SetCommTimeouts(handle, &timeouts)) return false;
    if (!SetCommMask(handle, EV_RXCHAR)) return false;
    DCB dcb = {
        .DCBlength    = sizeof(DCB),
        .BaudRate     = BAUD_RATE,
        .StopBits     = ONESTOPBIT,
        .Parity       = (BYTE)PARITY_NONE,
        .fOutxCtsFlow = false,
        .fRtsControl  = RTS_CONTROL_DISABLE,
        .fOutX        = false,
        .fInX         = false,
        .EofChar      = EOF, // @TODO: Should this be EOF or 0?
        .ByteSize     = 8,
    };
    if (!SetupComm(handle, 4096, 4096)) return false;
    if (!SetCommState(handle, &dcb)) return false;
    return true;
}

bool ping_test(void)
{
#define MSG_LEN 16
	const char msg[MSG_LEN] = "SPPPPING    ";
	void *file = CreateFile(PORT_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
	if (file == INVALID_HANDLE_VALUE) {
		printf("Failed to open file\n");
		return false;
	}
	COMMTIMEOUTS timeouts = {
		.ReadIntervalTimeout         = 50,
		.ReadTotalTimeoutConstant    = 50,
		.ReadTotalTimeoutMultiplier  = 10,
		.WriteTotalTimeoutConstant   = 50,
		.WriteTotalTimeoutMultiplier = 10,
	};
	AIL_ASSERT(SetCommTimeouts(file, &timeouts));
	AIL_ASSERT(SetCommMask(file, EV_RXCHAR));
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

	AIL_ASSERT(SetupComm(file, 4096, 4096));
	AIL_ASSERT(SetCommState(file, &dcb));
	DWORD written;
	AIL_ASSERT(WriteFile(file, msg, MSG_LEN, &written, 0));
	DWORD read;
	char reply[MSG_LEN] = {0};
	AIL_ASSERT(ReadFile(file, reply, MSG_LEN, &read, 0));
	printf("Received (len=%ld): '%s'\n", read, reply);
	CloseHandle(file);
	return true;
}

bool pidi_test(void)
{
#define CMDS_COUNT 4
	PidiCmd cmds[CMDS_COUNT] = {
		(PidiCmd) { .key = PIANO_KEY_A, .octave = -7, .on = true,  .time = 100,  .velocity = 100 },
		(PidiCmd) { .key = PIANO_KEY_A, .octave = -7, .on = false, .time = 1000, .velocity = 0   },
		(PidiCmd) { .key = PIANO_KEY_A, .octave = -7, .on = true,  .time = 2000, .velocity = 100 },
		(PidiCmd) { .key = PIANO_KEY_A, .octave = -7, .on = false, .time = 3000, .velocity = 0   },
	};

	comm_port = CreateFile(PORT_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
	if (comm_port == INVALID_HANDLE_VALUE) {
		printf("Failed to open file\n");
		return false;
	}

	ClientMsg msg = {
		.type = CMSG_PIDI,
		.data = { .pidi = {
			.time  = 0,
			.piano = comm_piano,
			.idx   = 0,
			.cmds  = cmds,
			.cmds_count = CMDS_COUNT,
		}},
	};
	bool res = send_msg(msg, false);
	CloseHandle(comm_port);
	return res;
}

AIL_RingBuffer rb = {0};

void listen_test(void)
{
	HANDLE port = CreateFile(PORT_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_SHARE_READ|FILE_SHARE_WRITE, 0);
	if (port == INVALID_HANDLE_VALUE) {
		printf("Failed to open port in reading\n");
		return;
	}
	if (!setup_port(port)) {
		printf("Failed to setup port\n");
		return;
	}

#define READING_CHUNK_SIZE 16
AIL_STATIC_ASSERT(READING_CHUNK_SIZE < AIL_RING_SIZE/2);
	u8 msg[READING_CHUNK_SIZE] = {0};
	f64 start   = ail_time_clock_start();
	f64 elapsed = 0.0f;

	while (true) {
		elapsed += ail_time_clock_elapsed(start);
		if (elapsed >= 1.0f) {
			DWORD written;
			printf("Writing ping...");
			if (!WriteFile(port, "SPPPPING    ", 12, &written, 0)) {
				printf("Failed to write message\n");
				return;
			}
			printf("Wrote ping\n");
			start   = ail_time_clock_start();
			elapsed = 0;
		}

		DWORD read;
		if (!ReadFile(port, msg, READING_CHUNK_SIZE, &read, 0)) {
			printf("Failed to read from port\n");
			CloseHandle(port);
			return;
		}
		ail_ring_writen(&rb, (u8)read, msg);

		if (ail_ring_len(rb) >= 12) {
			u32 tmp = ail_ring_peek4msb(rb);
			char *s = (char *)&tmp;
			printf("Magic: %c%c%c%c\n", s[3], s[2], s[1], s[0]);
			if (ail_ring_peek4msb(rb) != SPPP_MAGIC) {
				ail_ring_pop(&rb);
			} else {
				ail_ring_popn(&rb, 4);
				ServerMsgType type = ail_ring_read4msb(&rb);
				u32 n = ail_ring_read4lsb(&rb);
				(void)n;
				switch (type) {
					case SMSG_PONG:
						printf("Read message of type PONG\n");
						break;
					case SMSG_SUCC:
						printf("Read message of type SUCC\n");
						break;
					case SMSG_REQP:
						printf("Read message of type REQP\n");
						break;
					default:
						printf("Read message of unknown type\n");
						break;
				}
			}
		}
	}
	return;
}

int main(void)
{
	// if (!ping_test()) printf("Error in Ping-Test\n");
	// if (!pidi_test()) printf("Error in Pidi-Test\n");
	listen_test();

	// void *file = CreateFile(PORT_NAME, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
	// if (file == INVALID_HANDLE_VALUE) {
	// 	printf("Failed to open file\n");
	// 	return 1;
	// }
	// COMMTIMEOUTS timeouts = {
	// 	.ReadIntervalTimeout         = 50,
	// 	.ReadTotalTimeoutConstant    = 50,
	// 	.ReadTotalTimeoutMultiplier  = 10,
	// 	.WriteTotalTimeoutConstant   = 50,
	// 	.WriteTotalTimeoutMultiplier = 10,
	// };
	// AIL_ASSERT(SetCommTimeouts(file, &timeouts));
	// AIL_ASSERT(SetCommMask(file, EV_RXCHAR));
	// DCB dcb = {
	// 	.DCBlength    = sizeof(DCB),
	// 	.BaudRate     = BAUD_RATE,
	// 	.StopBits     = ONESTOPBIT,
	// 	.Parity       = (BYTE)PARITY_NONE,
	// 	.fOutxCtsFlow = false,
    // 	.fRtsControl  = RTS_CONTROL_DISABLE,
    // 	.fOutX        = false,
    // 	.fInX         = false,
	// 	.EofChar      = EOF,
	// 	.ByteSize     = 8,
	// };
	// AIL_ASSERT(SetupComm(file, 4096, 4096));
	// AIL_ASSERT(SetCommState(file, &dcb));
	// DWORD read;
	// char reply[MSG_LEN] = {0};
	// AIL_ASSERT(ReadFile(file, reply, MSG_LEN, &read, 0));
	// printf("Received (len=%ld): '%s'\n", read, reply);
	// CloseHandle(file);

	return 0;
}
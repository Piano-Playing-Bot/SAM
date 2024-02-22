#define AIL_ALL_IMPL
#define AIL_ALLOC_IMPL
#include "ail_alloc.h"
#include "common.h"
#include "comm.c"


bool ping_test(void)
{
#define MSG_LEN 16
	const char msg[MSG_LEN] = "SPPPPING    ";
	void *file = CreateFile("COM5", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
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

	comm_port = CreateFile("COM5", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
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

int main(void)
{
	if (!ping_test()) printf("Error in Ping-Test\n");
	if (!pidi_test()) printf("Error in Pidi-Test\n");

	// void *file = CreateFile("COM5", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
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
#include "ail_alloc.h"
#include "comm.c"
#include "common.h"

#define MSG_LEN 16

bool SetPortBoudRate(void *com_port, int rate)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return FALSE;
	dcbSerialParams.BaudRate = rate;
	Status = SetCommState(com_port, &dcbSerialParams);
	return Status;
}

int main(void)
{
	const char msg[MSG_LEN] = "SPPPPING    ";

#if 1
	void *file = CreateFile("COM4", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
	if (file == INVALID_HANDLE_VALUE) {
		printf("Failed to open file\n");
		return 1;
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
		.DCBlength = sizeof(DCB),
		.BaudRate  = BAUD_RATE,
	};
	AIL_ASSERT(SetCommState(file, &dcb));
	DWORD written;
	AIL_ASSERT(WriteFile(file, msg, MSG_LEN, &written, 0));
	// DWORD eventMask;
	// AIL_ASSERT(WaitCommEvent(file, &eventMask, 0));
	DWORD read;
	char reply[MSG_LEN] = {0};
	AIL_ASSERT(ReadFile(file, reply, MSG_LEN, &read, 0));
	printf("Received (len=%ld): '%s'\n", read, reply);
	CloseHandle(file);

#else
	u64 fd;
	if (ail_fs_open_file("COM4", &fd, true)) {
		DCB dcb = { 0 };
		dcb.DCBlength = sizeof(dcb);
		AIL_ASSERT(GetCommState((void *)fd, &dcb));
		dcb.BaudRate = BAUD_RATE;
		AIL_ASSERT(SetCommState((void *)fd, &dcb));

		if (ail_fs_write_n_bytes(fd, msg, MSG_LEN)) {
			// printf("Wrote msg successfully\n");
			u64 len;
			u8 reply[MAX_SERVER_MSG_SIZE] = {0};
			if (ail_fs_read_n_bytes(fd, reply, MAX_SERVER_MSG_SIZE, &len)) {
				printf("Received reply (len=%lld): '%s'\n", len, reply);
			}
		}
	}
#endif
}
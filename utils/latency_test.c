#include "header.h"
#include <windows.h>
#include <xpsprint.h>
#define AIL_TIME_IMPL
#include "ail_time.h"

bool setup_port(HANDLE port) {
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = 50,
        .ReadTotalTimeoutConstant    = 50,
        .ReadTotalTimeoutMultiplier  = 10,
        .WriteTotalTimeoutConstant   = 50,
        .WriteTotalTimeoutMultiplier = 10,
    };
    if (!SetCommTimeouts(port, &timeouts)) return false;
    if (!SetCommMask(port, EV_RXCHAR)) return false;
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
    if (!SetupComm(port, 4096, 4096)) return false;
    if (!SetCommState(port, &dcb)) return false;
    return true;
}

// Read all incoming data from the port into the Ring Buffer
int main(void)
{
	void *port = CreateFile("COM5", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
	if (port == INVALID_HANDLE_VALUE) {
		printf("Failed to open port\n");
		return 1;
	}
	if (!setup_port(port)) {
		printf("Failed to setup port\n");
		return 1;
	}

    u8 msg[4] = {0};
    DWORD read;
	DWORD written;

#define N 2048
	f64 latencies[N] = {0};
	for (u32 i = 0; i < N; i++) {
		f64 start = ail_time_clock_start();
		AIL_ASSERT(WriteFile(port, msg, 1, &written, 0));
		AIL_ASSERT(ReadFile(port, msg, 1, &read, 0));
		f64 elapsed = ail_time_clock_elapsed(start);
		AIL_ASSERT(written == 1);
		AIL_ASSERT(read == 1);
		latencies[i] = elapsed/2.0;
		printf(".");
	}
	printf("\n");

	f64 min = 1000000.0;
	f64 max = 0.0;
	f64 sum = 0.0;
	for (u32 i = 0; i < N; i++) {
		sum += latencies[i];
		if (latencies[i] > max) max = latencies[i];
		if (latencies[i] < min) min = latencies[i];
	}
	f64 avg = sum/N;

	printf("Min: %f\n", min);
	printf("Max: %f\n", max);
	printf("Avg: %f\n", avg);

	CloseHandle(port);
	return 0;
}
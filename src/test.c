#include "ail_alloc.h"
#include "comm.c"

#define MSG_LEN 16

int main(void)
{
	const char msg[MSG_LEN] = "SPPPPING    ";
	u64 fd;
	if (ail_fs_open_file("COM4", &fd, true)) {
		if (ail_fs_write_n_bytes(fd, msg, MSG_LEN)) {
			// printf("Wrote msg successfully\n");
			u64 len;
			u8 reply[MAX_SERVER_MSG_SIZE];
			if (ail_fs_read_n_bytes(fd, reply, MAX_SERVER_MSG_SIZE, &len)) {
				printf("Received reply (len=%lld): %s\n", len, reply);
			}
		}
	}

}
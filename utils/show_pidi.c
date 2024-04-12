#define AIL_ALL_IMPL
#define AIL_BUF_IMPL
#define AIL_FS_IMPL
#define MIDI_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "common.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("USAGE: %s <PIDI file>", argv[0]);
		return 1;
	}
	const char *input = argv[1];
	AIL_Buffer buf = ail_buf_from_file(input);
	AIL_ASSERT(ail_buf_read4msb(&buf) == PIDI_MAGIC);
	u32 msTime = ail_buf_read4lsb(&buf);
	printf("Length in ms: %d\n", msTime);
	while (buf.idx < buf.len - 1) {
		PidiCmd cmd = decode_cmd(&buf);
		print_cmd(cmd);
	}
	return 0;
}
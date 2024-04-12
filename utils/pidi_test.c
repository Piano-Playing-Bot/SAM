#define AIL_ALL_IMPL
#define AIL_BUF_IMPL
#define AIL_FS_IMPL
#define MIDI_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "common.h"
#include "midi.c"
#include <stdio.h>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("USAGE: %s <midi file>", argv[0]);
		return 1;
	}
	const char *input = argv[1];
	AIL_Buffer buf = ail_buf_from_file(input);
	ParseMidiRes res = parse_midi(buf);
	if (!res.succ) {
		printf("Error: %s\n", res.val.err);
	} else {
		u8 piano[KEYS_AMOUNT] = { 0 };
		PlayedKeyList played_keys = { 0 };
		AIL_DA(PidiCmd) cmds = res.val.song.cmds;
		u32 time = 0;
		for (u32 i = 0; i < cmds.len; i++) {
			PidiCmd cmd = cmds.data[i];
			time += pidi_dt(cmd);
			apply_pidi_cmd(time, cmd, piano, &played_keys);
			printf("\033[32mTime: %d, Command: ", time);
			print_cmd(cmd);
			printf("\033[0m");
			printf("[");
			for (u8 j = 0; j < KEYS_AMOUNT; j++) {
				if (j == 0) printf("%d", piano[j]);
				else printf(", %d", piano[j]);
			}
			printf("]\n");
		}
	}
	return 0;
}
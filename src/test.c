#define AIL_ALL_IMPL
#include "common.h"

bool cmd_eq(PidiCmd c1, PidiCmd c2)
{
	return (
		c1.key      == c2.key      &&
		c1.octave   == c2.octave   &&
		c1.dt       == c2.dt       &&
		c1.len      == c2.len      &&
		c1.velocity == c2.velocity
	);
}

int main(void)
{
	AIL_Buffer buffer = ail_buf_new(64);
	for (i8 octave = -8; octave < 8; octave++) {
		for (u8 key = 0; key < PIANO_KEY_AMOUNT; key++) {
			for (u8 velocity = 0; velocity < MAX_VELOCITY; velocity++) {
				buffer.idx = 0;
				buffer.len = 64;
				buffer.cap = 64;
				PidiCmd cmd = {
					.key = key,
					.octave = octave,
					.velocity = velocity,
					.dt = 480,
					.len = 20,
				};
				encode_cmd(&buffer, cmd);
				PidiCmd new_cmd = decode_cmd_simple(buffer.data);
				if (!cmd_eq(cmd, new_cmd)) {
					printf("Original Command: ");
					print_cmd(cmd);
					printf("Decoded Command:  ");
					print_cmd(new_cmd);
					AIL_ASSERT(false);
				}
			}
		}
	}

	printf("\033[32mTest successful!\033[0m\n");
	return 0;
}
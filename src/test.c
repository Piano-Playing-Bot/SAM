#define AIL_ALL_IMPL
#include "common.h"

bool cmd_eq(PidiCmd c1, PidiCmd c2)
{
	return (
		c1.key      == c2.key      &&
		c1.octave   == c2.octave   &&
		c1.on       == c2.on       &&
		c1.time     == c2.time     &&
		c1.velocity == c2.velocity
	);
}

int main(void)
{
	AIL_Buffer buffer = ail_buf_new(64);
	for (i8 octave = -8; octave < 8; octave++) {
		for (u8 key = 0; key < PIANO_KEY_AMOUNT; key++) {
			for (u8 velocity = 0; velocity < UINT8_MAX; velocity++) {
				for (u8 on = 0; on < 2; on++) {
					for (u64 time; time < 99999; time += 11) {
						buffer.idx = 0;
						buffer.len = 64;
						buffer.cap = 64;
						PidiCmd cmd = {
							.key = key,
							.octave = octave,
							.on = (bool)on,
							.velocity = velocity,
							.time = time,
						};
						encode_cmd(&buffer, cmd);
						PidiCmd new_cmd = decode_cmd(&buffer);
						if (cmd_eq(cmd, new_cmd)) {
							printf("Original Command: ");
							print_cmd(cmd);
							printf("\n");
							printf("Decoded Command: ");
							print_cmd(new_cmd);
							printf("\n");
							AIL_ASSERT(false);
						}
					}
				}
			}
		}
	}

	printf("Test successful!\n");
	return 0;
}
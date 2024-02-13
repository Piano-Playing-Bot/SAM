#define AIL_ALL_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#define MIDI_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "common.h"
#include "midi.c"
#include <stdio.h>
#include "string.h"

// void print_song(Song song)
// {
//     char *key_strs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
//     printf("{\n  name: %s\n  len: %lldms\n  chunks: [\n", song.name, song.len);
//     for (u32 i = 0; i < song.chunks.len; i++) {
//         MusicChunk c = song.chunks.data[i];
//         printf("    { key: %2s, octave: %2d, on: %c, time: %lld, len: %d }\n", key_strs[c.key], c.octave, c.on ? 'y' : 'n', c.time, c.len);
//     }
//     printf("  ]\n}\n");
// }

// Returns a new array, that contains first array a and then array b. Useful for adding strings for example
// a_size and b_size should both be the size in bytes, not the count of elements
void* memadd(const void *a, u64 a_size, const void *b, u64 b_size)
{
    char* out = malloc(a_size * b_size);
    memcpy(out, a, a_size);
    memcpy(&out[a_size], b, b_size);
    return (void*) out;
}

char* stradd(const char *a, const char *b)
{
	return (char *)memadd(a, strlen(a), b, strlen(b) + 1);
}

char* fname_sized(char *filename, u64 n)
{
	if (!n) return NULL;
	char *base = &filename[n - 1];
	bool found = false;
	while (!(found = (*base == '\\' || *base == '/')) && base > filename) base--;
	return base + found;
}

char* fname(char *filename)
{
	return fname_sized(filename, strlen(filename));
}

int main(int argc, char *argv[])
{
	if (argc != 2 && argc != 3) {
		printf("USAGE: %s <input> [<output_dir>]", argv[0]);
		return 1;
	}
	char *input  = argv[1];
	char *outdir = argc>=3 ? argv[2] : NULL;
	AIL_Buffer buf = ail_buf_from_file(input);
	ParseMidiRes res = parse_midi(buf);
	if (!res.succ) {
		printf("Error: %s\n", res.val.err);
	} else {
		if (outdir) {
			char *songname = fname(input);
			res.val.song.name = songname;
			const char *outfile = stradd(outdir, songname);
			remove(outfile);
			write_midi(res.val.song, outfile);
		} else print_song(res.val.song);
	}
	return 0;
}
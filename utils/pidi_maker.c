#define AIL_ALL_IMPL
#define AIL_BUF_IMPL
#define AIL_FS_IMPL
#define AIL_SV_IMPL
#define AIL_ALLOC_IMPL
#define MIDI_IMPL
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"
#include "ail_sv.h"
#include "ail_alloc.h"
#include "common.h"
#include "midi.c"
#include <stdio.h>
#include <windows.h>
#include <conio.h>

void clearScreen()
{
    COORD topLeft  = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(
        console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );
    FillConsoleOutputAttribute(
        console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE,
        screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );
    SetConsoleCursorPosition(console, topLeft);
}

bool save_pidi(Song song, AIL_SV data_dir_path)
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write4msb(&buf, PIDI_MAGIC);
    ail_buf_write4lsb(&buf, song.cmds.len);
    for (u32 i = 0; i < song.cmds.len; i++) {
        encode_cmd(&buf, song.cmds.data[i]);
    }

    u64 data_dir_path_len = data_dir_path.len;
    u64 name_len          = strlen(song.name);
    char *fname = malloc(data_dir_path_len + name_len + 6);
    memcpy(fname, data_dir_path.str, data_dir_path_len);
    memcpy(&fname[data_dir_path_len], song.name, name_len);
    memcpy(&fname[data_dir_path_len + name_len], ".pidi", 6);
    bool out = ail_buf_to_file(&buf, fname);
    free(fname);
    return out;
}

bool save_library(AIL_DA(Song) library, AIL_SV data_dir_path, AIL_SV library_filepath)
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write4msb(&buf, PDIL_MAGIC);
    ail_buf_write4lsb(&buf, library.len);
    for (u32 i = 0; i < library.len; i++) {
        Song song = library.data[i];
        u32 name_len  = strlen(song.name);
        ail_buf_write4lsb(&buf, name_len);
        ail_buf_write8lsb(&buf, song.len);
        ail_buf_writestr(&buf, song.name, name_len);
    }
    if (!ail_fs_dir_exists(data_dir_path.str)) mkdir(data_dir_path.str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    return ail_buf_to_file(&buf, library_filepath.str);
}

AIL_DA(Song) load_library(AIL_SV data_dir_path, AIL_SV library_filepath)
{
	AIL_DA(Song) library = ail_da_new_empty(Song);
    if (!ail_fs_dir_exists(data_dir_path.str)) {
        mkdir(data_dir_path.str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        goto nothing_to_load;
    } else {
        if (!ail_fs_is_file(library_filepath.str)) goto nothing_to_load;
        AIL_Buffer buf = ail_buf_from_file(library_filepath.str);
        if (ail_buf_read4msb(&buf) != PDIL_MAGIC) goto nothing_to_load;
        u32 n = ail_buf_read4lsb(&buf);
        ail_da_maybe_grow(&library, n);
        for (; n > 0; n--) {
            u32 name_len = ail_buf_read4lsb(&buf);
            u64 song_len = ail_buf_read8lsb(&buf);
            char *name   = ail_buf_readstr(&buf, name_len);
            Song song = {
                .name   = name,
                .len    = song_len,
                .cmds = ail_da_new_empty(PidiCmd),
            };
            ail_da_push(&library, song);
        }
        goto end;
    }

nothing_to_load:
    ail_da_maybe_grow(&library, 16);
end:
    return library;
}

u64 get_song_len(AIL_DA(PidiCmd) cmds)
{
	u64 len = 0;
	u32 cur_time = 0;
	for (u32 i = 0; i < cmds.len; i++) {
		cur_time += cmds.data[i].dt;
		len = AIL_MAX(len, cur_time + cmds.data[i].len*LEN_FACTOR);
	}
	return len;
}

void get_input_no_exit(AIL_Str *out)
{
	out->len = 0;
	char c;
	while ((c = getc(stdin)) != '\n') out->str[out->len++] = c;
	AIL_SV x = ail_sv_trim(ail_sv_from_str(*out));
	out->str = (char *)x.str;
	out->len = x.len;
	out->str[out->len] = 0;
}

// Returns true if 'q' was pressed
bool get_input(AIL_Str *out)
{
	out->len = 0;
	char c;
	while ((c = getc(stdin)) != '\n') {
		out->str[out->len++] = c;
		if (c == 'q' || c == 'Q') return true;
	}
	AIL_SV x = ail_sv_trim(ail_sv_from_str(*out));
	out->str = (char *)x.str;
	out->len = x.len;
	out->str[out->len] = 0;
	return false;
}

void from_piano_idx(u8 idx, PidiCmd *cmd)
{
	for (i8 octave = -4; octave < 5; octave++) {
		for (PianoKey key = 0; key < PIANO_KEY_AMOUNT; key++) {
			if (idx == get_piano_idx(key, octave)) {
				cmd->key    = key;
				cmd->octave = octave;
				return;
			}
		}
	}
	printf("Invalid state reached in from_piano_idx");
	exit(1);

// #define FIRST_OCTAVE_LEN (PIANO_KEY_AMOUNT - STARTING_KEY)
// 	if (idx < FIRST_OCTAVE_LEN) {
// 		cmd->octave = -((FULL_OCTAVES_AMOUNT + (FIRST_OCTAVE_LEN > 0))/2);
// 		cmd->key    = STARTING_KEY + idx;
// 	} else if (idx < MID_OCTAVE_START_IDX) {
// 		cmd->octave = (idx - MID_OCTAVE_START_IDX)/PIANO_KEY_AMOUNT - 1;
// 		cmd->key    = (idx - FIRST_OCTAVE_LEN)%PIANO_KEY_AMOUNT;
// 	} else {
// 		cmd->octave = (idx - MID_OCTAVE_START_IDX)/PIANO_KEY_AMOUNT;
// 		cmd->key    = (idx - FIRST_OCTAVE_LEN)%PIANO_KEY_AMOUNT;
// 	}

// 	if (get_piano_idx(pidi_key(*cmd), pidi_octave(*cmd)) != idx) {
// 		printf("Assertion broken in from_piano_idx:\n");
// 		printf("  idx=%d -> key=%d & octave=%d -> idx=%d\n", idx, pidi_key(*cmd), pidi_octave(*cmd), get_piano_idx(pidi_key(*cmd), pidi_octave(*cmd)));
// 		exit(1);
// 	}
}

int main(void)
{
	AIL_SV data_dir_path    = ail_sv_from_cstr("./bin/data/");
	AIL_SV library_filepath = ail_sv_from_cstr("./bin/data/library.pdil");
	AIL_DA(PidiCmd) cmds = ail_da_new(PidiCmd);
	// AIL_Allocator arena = ail_alloc_arena_new(AIL_ALLOC_PAGE_SIZE, &ail_alloc_pager);
#define BUFFER_LEN 2048
	char input_buffer[BUFFER_LEN] = { 0 };
	AIL_Str input = ail_str_from_parts(input_buffer, BUFFER_LEN);
	PidiCmd cmd = { 0 };
	u32 len;

	while (true) {
		AIL_Str in = input;
		clearScreen();
		if (cmds.len) {
			printf("Last added note: ");
			print_cmd(cmds.data[cmds.len - 1]);
		}

		printf("To stop adding new notes, press 'q' at any time.\n");
		printf("When you are happy with your input, press Enter.\n");
		printf("\n");
get_idx:
		printf("Index of Key on the Piano (press Enter to provide Key and octave instead): ");
		if (get_input(&in)) break;
		if (input.len != 0) {
			u64 idx = ail_sv_parse_unsigned(ail_sv_from_str(in), &len);
			if (!len) goto get_idx;
			from_piano_idx(idx, &cmd);
		} else {
get_key:
			printf("Key (C, C#, etc.): ");
			if (get_input(&in)) break;
			if (input.len != 1 && input.len != 2) goto get_key;
			bool hashed = (input.len > 1) && (input.str[1] == '#');
			switch (input.str[0]) {
				case 'a':
				case 'A':
					if (hashed) cmd.key = PIANO_KEY_AS;
					else        cmd.key = PIANO_KEY_A;
					break;
				case 'b':
				case 'B':
					if (hashed) goto get_key;
					else        cmd.key = PIANO_KEY_B;
					break;
				case 'c':
				case 'C':
					if (hashed) cmd.key = PIANO_KEY_CS;
					else        cmd.key = PIANO_KEY_C;
					break;
				case 'd':
				case 'D':
					if (hashed) cmd.key = PIANO_KEY_DS;
					else        cmd.key = PIANO_KEY_D;
					break;
				case 'e':
				case 'E':
					if (hashed) goto get_key;
					else        cmd.key = PIANO_KEY_E;
					break;
				case 'f':
				case 'F':
					if (hashed) cmd.key = PIANO_KEY_FS;
					else        cmd.key = PIANO_KEY_F;
					break;
				case 'g':
				case 'G':
					if (hashed) cmd.key = PIANO_KEY_GS;
					else        cmd.key = PIANO_KEY_G;
					break;
				default:
					goto get_key;
			}
get_octave:
			printf("Octave (-4 to 4): ");
			if (get_input(&in)) break;
			cmd.octave = ail_sv_parse_signed(ail_sv_from_str(in), &len);
			if (!len) goto get_octave;
		}
get_velocity:
		printf("Velocity (0 to 15): ");
		if (get_input(&in)) break;
		cmd.velocity = ail_sv_parse_unsigned(ail_sv_from_str(in), &len);
		if (!len) goto get_velocity;
get_dt:
		printf("Time to play at since last note started playing in ms: ");
		if (get_input(&in)) break;
		cmd.dt = ail_sv_parse_unsigned(ail_sv_from_str(in), &len);
		if (!len) goto get_dt;
get_len:
		printf("Length in milliseconds to play the note for: ");
		if (get_input(&in)) break;
		cmd.len = ail_sv_parse_unsigned(ail_sv_from_str(in), &len)/LEN_FACTOR;
		if (!len) goto get_len;

		ail_da_push(&cmds, cmd);
	}

	clearScreen();
	printf("All notes:\n");
	for (u32 i = 0; i < cmds.len; i++) {
		print_cmd(cmds.data[i]);
	}
	printf("\n");

get_fname:
	printf("How would you like to name the song? ");
	get_input_no_exit(&input);
	if (!input.len) goto get_fname;
	AIL_Str fpath = ail_sv_concat(data_dir_path, ail_sv_from_str(input));
	if (ail_fs_is_file(fpath.str)) goto get_fname;

	Song song = {
		.cmds = cmds,
		.len  = get_song_len(cmds),
		.name = input.str,
	};
	if (!save_pidi(song, data_dir_path)) {
		printf("Failed to save PIDI file '%s' :(\n", song.name);
		return 1;
	}
	AIL_DA(Song) songs = load_library(data_dir_path, library_filepath);
	ail_da_push(&songs, song);
	if (!save_library(songs, data_dir_path, library_filepath)) {
		printf("Failed to save Library file :(\n");
		return 1;
	}
	printf("Song saved as '%s' successfully\n", song.name);
	return 0;
}
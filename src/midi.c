#define AIL_ALL_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#include "common.h"
#include <stdbool.h> // For boolean definitions
#include <stdlib.h>  // For malloc, memcpy, free
#include "ail.h"
#include "ail_fs.h"
#include "ail_buf.h"

// @TODO: Use custom allocators instead of malloc here

typedef AIL_DA(PidiCmd) PidiCmdList;
AIL_DA_INIT(PidiCmdList);

typedef union {
	Song song;
	char err[256];
} ParseMidiResVal;

typedef struct {
	bool succ;
	ParseMidiResVal val;
} ParseMidiRes;

typedef struct PidiCmdTimed {
    PidiCmd cmd;
    u32 time;
} PidiCmdTimed;
AIL_DA_INIT(PidiCmdTimed);

#define MIDI_MAX_VELOCITY 127
#define MIDI_MIN_CMD_SIZE 3 // at least delta_time: 1, key: 1, velocity: 1
#define MIDI_0KEY_OCTAVE -5
#define MIDI_NOTE_TO_OCTAVE(note) ((MIDI_0KEY_OCTAVE + ((note) / PIANO_KEY_AMOUNT)))
#define MIDI_NOTE_TO_KEY(note)    ((note) % PIANO_KEY_AMOUNT)
#define MIDI_TICKS_TO_MS(ticks, tempo, ticksPQN) ((ticks) * (u32)(((f32)(tempo) / (f32)(ticksPQN)) / 1000.0f))

u32 read_var_len(AIL_Buffer *buffer);
ParseMidiRes parse_midi(AIL_Buffer buffer);
void write_midi(Song song, const char *fpath);
void sort_chunks(AIL_DA(PidiCmd) cmds);
void write_timed_midi(const PidiCmdTimed *cmds, u32 len, const char *fpath);


ParseMidiRes merge_sorted_chunks(AIL_DA(PidiCmdList) chunks, u64 *start_times) {
    ParseMidiResVal res = {0};
    u32 total_count = 0;
    for (u32 i = 0; i < chunks.len; i++) total_count += chunks.data[i].len;
    AIL_DA(PidiCmd) cmds = ail_da_new_with_cap(PidiCmd, total_count);
    cmds.len = total_count;
    u32 *indices = calloc(chunks.len, sizeof(u32));
    u64 cur_time = 0; // Start-Time (in ms) of the last inserted command
    u64 song_len = 0; // length of song in ms

    for (u32 i = 0; i < total_count; i++) {
        i32 min = -1;
        for (i32 j = 0; j < (i32)chunks.len; j++) {
            if ((indices[j] < chunks.data[j].len) &&
                ((min < 0) || (start_times[j] + chunks.data[j].data[indices[j]].dt < start_times[min] + chunks.data[min].data[indices[min]].dt))) {
                min = j;
            }
        }
        AIL_ASSERT(min >= 0);
        start_times[min] += chunks.data[min].data[indices[min]].dt;
        cmds.data[i]    = chunks.data[min].data[indices[min]];
        AIL_ASSERT(start_times[min] >= cur_time);
        cmds.data[i].dt = start_times[min] - cur_time;
        cur_time       += cmds.data[i].dt;
        song_len        = AIL_MAX(song_len, cur_time + cmds.data[i].len*LEN_FACTOR);
        indices[min]++;
    }

    free(indices);
    res.song.cmds = cmds;
    res.song.len  = song_len;
    return (ParseMidiRes) {true, res};
}

// Code taken from MIDI Standard
u32 read_var_len(AIL_Buffer *buffer)
{
    u32 value;
    u8 c;
    if ((value = ail_buf_read1(buffer)) & 0x80) {
        value &= 0x7f;
        do {
            value = (value << 7) + ((c = ail_buf_read1(buffer)) & 0x7f);
        } while (c & 0x80); }
    return value;
}

ParseMidiRes parse_midi(AIL_Buffer buffer)
{
    ParseMidiResVal val = {0};
    #define midiFileStartLen 8
    const char midiFileStart[midiFileStartLen] = {'M', 'T', 'h', 'd', 0, 0, 0, 6};

    if (buffer.len < 14 || memcmp(buffer.data, midiFileStart, midiFileStartLen) != 0) {
        sprintf(val.err, "Invalid Midi File provided.\nMake sure the File wasn't corrupted\n");
        return (ParseMidiRes) { false, val };
    }
    buffer.idx += midiFileStartLen;

    u32 tempo = 500000; // in ms. 500000ms = 120BPM (default value)
    u16 format   = ail_buf_read2msb(&buffer);
    u16 ntrcks   = ail_buf_read2msb(&buffer);
    u16 ticksPQN = ail_buf_read2msb(&buffer);
    if (ticksPQN & 0x8000) {
        // If first bit is set, a different encoding is used for some reason
        AIL_TODO();
    }

    DBG_LOG("format: %d, ntrcks: %d, ticks per quarter-note: %d\n", format, ntrcks, ticksPQN);

    if (format > 2) {
        sprintf(val.err, "Unknown Midi Format.\nPlease try a different Midi File\n");
        return (ParseMidiRes) { false, val };
    }

    AIL_DA(PidiCmdList) pidi_chunks = ail_da_new_with_cap(PidiCmdList, ntrcks);
    AIL_DA(u64)         start_times = ail_da_new_with_cap(u64, ntrcks);
    for (u16 i = 0; i < ntrcks; i++) {
        u8 command = 0; // used in running status (@Note: status == command)
        u8 channel = 0; // used in running status
        u32 last_cmd_dt = 0;
        AIL_UNUSED(channel);
        // Parse track cmds
        AIL_ASSERT(ail_buf_read4msb(&buffer) == 0x4D54726B);
        u32 chunk_len   = ail_buf_read4msb(&buffer);
        u32 chunk_end   = buffer.idx + chunk_len;
        AIL_DA(PidiCmd) pidi_chunk = ail_da_new_with_cap(PidiCmd, chunk_len/MIDI_MIN_CMD_SIZE);
        DBG_LOG("Parsing cmd from %#010llx to %#010x\n", buffer.idx, chunk_end);
        while (buffer.idx < chunk_end) {
            // Parse MTrk events
            u32 delta_time  = read_var_len(&buffer);
            last_cmd_dt    += delta_time;
            // DBG_LOG("index: %#010llx, delta_time: %d\n", buffer.idx, delta_time);
            if (ail_buf_peek1(buffer) == 0xff) {
                buffer.idx++;
                // Meta Event
                switch (ail_buf_read1(&buffer)) {
                    case 0x00: { // Sequence Number - ignored
                        AIL_ASSERT(ail_buf_read1(&buffer) == 2);
                        buffer.idx += 2;
                    } break;
                    case 0x01:   // Text Event          - ignored
                    case 0x02:   // Copyright Notice    - ignored
                    case 0x03:   // Sequence/Track Name - ignored
                    case 0x04:   // Instrument Name     - ignored
                    case 0x05:   // Lyric               - ignored
                    case 0x06:   // Marker              - ignored
                    case 0x07: { // Cue Point           - ignored
                        u32 len = read_var_len(&buffer);
                        buffer.idx += len;
                    } break;
                    case 0x20: { // MIDI Channel Prefix - ignored for now
                        // @Note: This secified that the next events only effect this specific channel
                        // @TODO: This should be handled if there are any events that may effect one channel and should not effect the notes from other channels
                        AIL_ASSERT(ail_buf_read1(&buffer) == 1);
                        buffer.idx++;
                    } break;
                    case 0x2f: { // End of Track - ignored
                        AIL_ASSERT(ail_buf_read1(&buffer) == 0);
                        AIL_ASSERT(buffer.idx == chunk_end);
                    } break;
                    case 0x51: { // Set Tempo
                        AIL_ASSERT(ail_buf_read1(&buffer) == 3);
                        // u32 x = ail_buf_read3msb(&buffer);
                        tempo = ail_buf_read3msb(&buffer);
                        DBG_LOG("tempo: %u\n", tempo);
                    } break;
                    case 0x54: { // SMPTE Offset
                        AIL_ASSERT(ail_buf_read1(&buffer) == 5);
                        u8 hr = ail_buf_read1(&buffer);
                        u8 mn = ail_buf_read1(&buffer);
                        u8 se = ail_buf_read1(&buffer);
                        u8 fr = ail_buf_read1(&buffer);
                        u8 ff = ail_buf_read1(&buffer);
                        // > This event, if present, designates the SMPTE time at which the track cmd is supposed to start
                        // @Study: Can we ignore this event?
                        DBG_LOG("SMPTE - hour: %u, min: %u, sec: %u, fr: %u, ff: %u\n", hr, mn, se, fr, ff);
                        AIL_TODO();
                    } break;
                    case 0x58: { // Time Signature - ignored
                        AIL_ASSERT(ail_buf_read1(&buffer) == 4);
                        buffer.idx += 2; // ignore num/den
                        // ticksPQN = ail_buf_read1(&buffer);
                        // u8 b = ail_buf_read1(&buffer);
                        // DBG_LOG("b: %u\n", b); // @Bug
                        buffer.idx += 2;
                        // AIL_ASSERT(b == 8); // It would be weird if there's not exactly 8 32nd notes ber quarter-note
                    } break;
                    case 0x59: { // Key Signature - ignored
                        AIL_ASSERT(ail_buf_read1(&buffer) == 2);
                        buffer.idx += 2;
                    } break;
                    case 0x7f: { // Sequencer-Specific Meta-Event - ignored
                        u32 len = read_var_len(&buffer);
                        buffer.idx += len;
                    } break;
                    default: {
                        buffer.idx -= 2;
                        u16 ev = ail_buf_read2msb(&buffer);
                        DBG_LOG("\033[33mEncountered unknown meta event %#04x.\033[0m\n", ev);
                        u8 len = ail_buf_read1(&buffer);
                        buffer.idx += len;
                    }
                }
            }
            else {
                // If current byte doesn't start with a 1, the running status is used
                if (ail_buf_peek1(buffer) & 0x80) {
                    command = (ail_buf_peek1(buffer) & 0xf0) >> 4;
                    channel = ail_buf_read1(&buffer) & 0x0f;
                    // DBG_LOG("New Status - ");
                } else {
                    // DBG_LOG("Running Status - ");
                }
                // DBG_LOG("Command: %#01x, Channel: %#01x\n", command, channel);
                switch (command) {
                    case 0x8:
                    case 0x9: { // Note off/on
                        u8 note     = ail_buf_read1(&buffer);
                        u8 velocity = ail_buf_read1(&buffer);
                        i8 octave   = MIDI_NOTE_TO_OCTAVE(note);
                        printf("octave: %d\n", octave);
                        u8 key      = MIDI_NOTE_TO_KEY(note);
                        if (command == 0x8 || !velocity) { // Note off
                            u16 len     = MIDI_TICKS_TO_MS(last_cmd_dt, tempo, ticksPQN);
                            // DBG_LOG("Note off: key=%d, octave=%d, len=%d\n", key, octave, len);
                            for (u32 k = pidi_chunk.len - 1; k < pidi_chunk.len; k--) {
                                PidiCmd *cmd = &pidi_chunk.data[k];
                                if (pidi_key(*cmd) == key && pidi_octave(*cmd) == octave) {
                                    cmd->len = (len + LEN_FACTOR/2)/LEN_FACTOR; // +LEN_FACTOR/2 to do rounding
                                    // DBG_LOG("Found note: ");
                                    // print_cmd(*cmd);
                                    break;
                                } else {
                                    len += cmd->dt;
                                }
                            }
                        } else { // Note on
                            PidiCmd cmd = {
                                .dt       = MIDI_TICKS_TO_MS(last_cmd_dt, tempo, ticksPQN),
                                .velocity = AIL_LERP((f32)velocity/MIDI_MAX_VELOCITY, 0, MAX_VELOCITY),
                                .len      = 0,
                                .octave   = octave,
                                .key      = key,
                            };
                            ail_da_push(&pidi_chunk, cmd);
                            DBG_LOG("\033[32mNote on: \033[0m");
                            print_cmd(cmd);
                            last_cmd_dt = 0;
                        }
                    } break;
                    case 0xA: { // Polyphonic Key Pressure
                        AIL_TODO();
                    } break;
                    case 0xB: { // Control Change
                        u8 c = ail_buf_read1(&buffer);
                        u8 v = ail_buf_read1(&buffer);
                        // DBG_LOG("Control Change: c = %#01x, v = %#01x\n", c, v);
                        AIL_ASSERT(v <= 127);
                        if (c < 120) {
                            // Do nothing for now
                            // @TODO: Check if any messages here might be interesting for us
                        } else switch (c) {
                            case 120: { // All Sound off @TODO
                                AIL_TODO();
                            } break;
                            case 121: { // Reset all controllers - ignored
                            } break;
                            case 122: {
                                AIL_TODO();
                            } break;
                            case 123: { // All Notes off
                                AIL_TODO();
                            } break;
                            case 124: {
                                AIL_TODO();
                            } break;
                            case 125: {
                                AIL_TODO();
                            } break;
                            case 126: {
                                AIL_TODO();
                            } break;
                            case 127: {
                                AIL_TODO();
                            } break;
                            default: AIL_UNREACHABLE();
                        }
                    } break;
                    case 0xC: { // Program Change - ignored
                        u8 patch = ail_buf_read1(&buffer);
                        DBG_LOG("Program Change: patch = %d\n", patch);
                        // Do nothing
                    } break;
                    case 0xD: { // Channel Pressure
                        AIL_TODO();
                    } break;
                    case 0xE: { // Pitch Bend Change
                        AIL_TODO();
                    } break;
                    case 0xF: { // System Common Messages
                        AIL_TODO();
                    } break;
                }
            }
        }
        ail_da_push(&pidi_chunks, pidi_chunk);
        ail_da_push(&start_times, 0);
    }

    return merge_sorted_chunks(pidi_chunks, start_times.data);
}

void write_midi(Song song, const char *fpath)
{
    AIL_DA(PidiCmdTimed) cmds = ail_da_new_with_cap(PidiCmdTimed, song.cmds.len*2);
    for (u32 i = 0, time = 0; i < song.cmds.len; i++) {
        time += song.cmds.data[i].dt;
        PidiCmdTimed on = {
            .cmd  = song.cmds.data[i],
            .time = time,
        };
        PidiCmdTimed off = {
            .cmd = (PidiCmd) {
                .velocity = 0,
                .octave   = on.cmd.octave,
                .key      = on.cmd.key,
            },
            .time = time + on.cmd.len*LEN_FACTOR,
        };
        ail_da_push(&cmds, on);
        ail_da_push(&cmds, off);
    }

    // Sort cmds
    for (u32 i = 0; i < cmds.len-1; i++) {
        u32 min = i;
        for (u32 j = i+1; j < cmds.len; j++) {
            if (cmds.data[j].time < cmds.data[min].time) min = j;
        }
        AIL_SWAP_PORTABLE(PidiCmdTimed, cmds.data[min], cmds.data[i]);
    }
    for (u32 i = 0; i < cmds.len; i++) {
        printf("time: %4dms, ", cmds.data[i].time);
        print_cmd(cmds.data[i].cmd);
    }

    write_timed_midi(cmds.data, cmds.len, fpath);
}

void write_timed_midi(const PidiCmdTimed *cmds, u32 len, const char *fpath)
{
    // DBG_LOG("Writing %s back to midi in %s\n", song.name, fpath);
    AIL_Buffer buffer = ail_buf_new(2048);
    u16 ticksPQN = 480;
    u32 tempo = 705882; // 500000;
    ail_buf_write1(&buffer, 'M');
    ail_buf_write1(&buffer, 'T');
    ail_buf_write1(&buffer, 'h');
    ail_buf_write1(&buffer, 'd');
    ail_buf_write4msb(&buffer, 6);
    ail_buf_write2msb(&buffer, 0);
    ail_buf_write2msb(&buffer, 1);
    ail_buf_write2msb(&buffer, ticksPQN);

    ail_buf_write1(&buffer, 'M');
    ail_buf_write1(&buffer, 'T');
    ail_buf_write1(&buffer, 'r');
    ail_buf_write1(&buffer, 'k');
    u64 len_idx = buffer.idx;
    buffer.idx += 4;

    ail_buf_write3msb(&buffer, 0x00ff03); // delta_time + Status for name
    char *trackname = "Grand Piano";
    ail_buf_write1(&buffer, strlen(trackname));
    ail_buf_writestr(&buffer, trackname, strlen(trackname));

    ail_buf_write3msb(&buffer, 0x00ff58); // delta_time + Status for Time-Signature
    ail_buf_write1(&buffer, 4);
    ail_buf_write4msb(&buffer, 0x04021808); // Time-Signature

    ail_buf_write3msb(&buffer, 0x00ff59); // delta_time + Status for Key-Signature
    ail_buf_write3msb(&buffer, 0x020000); // len + Key-Signature

    // Tempo
    ail_buf_write3msb(&buffer, 0x00ff51);
    ail_buf_write1(&buffer, 3);
    ail_buf_write3msb(&buffer, tempo);

    // Control Changes
    ail_buf_write4msb(&buffer, 0x00b07900);
    ail_buf_write3msb(&buffer, 0x006400);
    ail_buf_write3msb(&buffer, 0x006500);
    ail_buf_write3msb(&buffer, 0x00060c);
    ail_buf_write3msb(&buffer, 0x00647f);
    ail_buf_write3msb(&buffer, 0x00657f);
    ail_buf_write3msb(&buffer, 0x00c000);
    ail_buf_write4msb(&buffer, 0x00b00764);
    ail_buf_write3msb(&buffer, 0x000a40);
    ail_buf_write3msb(&buffer, 0x005b00);
    ail_buf_write3msb(&buffer, 0x005d00);
    ail_buf_write3msb(&buffer, 0x00ff21);
    ail_buf_write2msb(&buffer, 0x0100);

    u32 last_tick = 0;
    for (u32 i = 0; i < len; i++) {
        PidiCmdTimed c = cmds[i];
        u32 cur_tick   = c.time / (u64)(((f32)tempo / (f32)ticksPQN) / 1000.0f);
        u32 delta_time = cur_tick - last_tick;
        last_tick      = cur_tick;

        // Write variable length field for delta_time
        u32 x = delta_time & 0x7f;
        while ((delta_time >>= 7) > 0) {
            x <<= 8;
            x |= 0x80;
            x += (delta_time & 0x7f);
        }
        while (true) {
            ail_buf_write1(&buffer, (u8)x);
            if (x & 0x80) x >>= 8;
            else break;
        }

        if (i == 0) ail_buf_write1(&buffer, 0x90);
        ail_buf_write1(&buffer, (pidi_octave(c.cmd) - MIDI_0KEY_OCTAVE)*PIANO_KEY_AMOUNT + pidi_key(c.cmd));
        ail_buf_write1(&buffer, (u32)AIL_LERP((f32)c.cmd.velocity/(f32)(MAX_VELOCITY), 0, MIDI_MAX_VELOCITY));
    }

    ail_buf_write4msb(&buffer, 0x01ff2f00);

    u64 cur_idx = buffer.idx;
    buffer.idx  = len_idx;
    ail_buf_write4msb(&buffer, cur_idx - (len_idx + 4));
    buffer.idx  = cur_idx;

    ail_buf_to_file(&buffer, fpath);
    DBG_LOG("Done writing midi file to '%s'\n", fpath);
}
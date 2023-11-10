#ifndef MIDI_H_
#define MIDI_H_

#include "common.h"
#include <stdbool.h> // For boolean definitions
#include <stdlib.h>  // For malloc, memcpy
#define AIL_ALL_IMPL
#include "ail.h"
#define AIL_FS_IMPL
#include "ail_fs.h"
#define AIL_BUF_IMPL
#include "ail_buf.h"

typedef union {
	Song song;
	char err[256];
} ParseMidiResVal;

typedef struct {
	bool succ;
	ParseMidiResVal val;
} ParseMidiRes;


// MIDI Stuff

#define MIDI_0KEY_OCTAVE -5

typedef struct {
    u8 num;    // Numerator
    u8 den;    // Denominator expressed as a (negative) power of 2
    u8 clocks; // Number of MIDI-Clocks in a metronome click
    u8 b;      // Number of notate 32nd notes in quarter-notes (aka in 24 MIDI-Clocks)
} MIDI_Time_Signature;

typedef struct { // See definition in Spec
    i8 sf;
    i8 mi;
} MIDI_Key_Signature;

u32 read_var_len(AIL_Buffer *buffer);
ParseMidiRes parse_midi(char *filePath);
void write_midi(Song song, char *fpath);

#endif // MIDI_H_

#ifdef MIDI_IMPL
#ifndef _MIDI_IMPL_GUARD_
#define _MIDI_IMPL_GUARD_

#ifdef DEBUG
#include <stdio.h> // For printf - only used for debugging
#define dbg_log(...) printf(__VA_ARGS__)
#else
#define dbg_log(...) do { if (0) printf(__VA_ARGS__); } while(0)
#endif

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

ParseMidiRes parse_midi(char *filePath)
{
    ParseMidiResVal val = {0};
    i32   pathLen  = strlen(filePath);
    char *fileName = filePath;
    i32   nameLen  = pathLen;
    for (i32 i = pathLen-2; i > 0; i--) {
        if (filePath[i] == '/' || (filePath[i] == '\\' && filePath[i+1] != ' ')) {
            fileName = &filePath[i+1];
            nameLen  = pathLen - 1 - i;
            break;
        }
    }

    #define EXT_LEN 4
    if (pathLen < EXT_LEN || memcmp(&filePath[pathLen - EXT_LEN], ".mid", EXT_LEN) != 0) {
        sprintf(val.err, "%s is not a midi file\n", fileName);
        return (ParseMidiRes) { false, val };
    }

    AIL_Buffer buffer = ail_buf_from_file(filePath);
    // Remove file-ending from filename
    nameLen -= EXT_LEN;
    val.song.name = malloc((nameLen + 1) * sizeof(char));
    memcpy(val.song.name, fileName, nameLen);
    val.song.name[nameLen] = 0;

    #define midiFileStartLen 8
    const char midiFileStart[midiFileStartLen] = {'M', 'T', 'h', 'd', 0, 0, 0, 6};

    if (buffer.len < 14 || memcmp(buffer.data, midiFileStart, midiFileStartLen) != 0) {
        sprintf(val.err, "Invalid Midi File provided.\nMake sure the File wasn't corrupted\n");
        return (ParseMidiRes) { false, val };
    }
    buffer.idx += midiFileStartLen;

    u32 tempo = 500000; // in ms. 500000ms = 120BPM (default value)
    MIDI_Time_Signature time_signature = { // Initialized to 4/4
        .num = 4,
        .den = 2,
        .clocks = 18,
        .b = 8,
    };
    MIDI_Key_Signature key_signature = {0};

    u16 format   = ail_buf_read2msb(&buffer);
    u16 ntrcks   = ail_buf_read2msb(&buffer);
    u16 ticksPQN = ail_buf_read2msb(&buffer);
    if (ticksPQN & 0x8000) {
        // If first bit is set, a different encoding is used for some reason
        AIL_TODO();
    }

    dbg_log("format: %d, ntrcks: %d, ticks per quarter-note: %d\n", format, ntrcks, ticksPQN);

    if (format > 2) {
        sprintf(val.err, "Unknown Midi Format.\nPlease try a different Midi File\n");
        return (ParseMidiRes) { false, val };
    }

    u8 command = 0; // used in running status (@Note: status == command)
    u8 channel = 0; // used in running status
    for (u16 i = 0; i < ntrcks; i++) {
        // Parse track chunks
        u64 ticks  = 0; // Amount of ticks of the virtual midi clock
        AIL_ASSERT(ail_buf_read4msb(&buffer) == 0x4D54726B);
        u32 chunk_len   = ail_buf_read4msb(&buffer);
        u32 chunk_end   = buffer.idx + chunk_len;
        dbg_log("Parsing chunk from %#010llx to %#010x\n", buffer.idx, chunk_end);
        while (buffer.idx < chunk_end) {
            // Parse MTrk events
            u32 delta_time = read_var_len(&buffer);
            dbg_log("delta_time: %d\n", delta_time);
            ticks += delta_time;
            if (ail_buf_peek1(buffer) == 0xff) {
                buffer.idx++;
                // Meta Event
                switch (ail_buf_read1(&buffer)) {
                    case 0x00: {
                        AIL_TODO();
                    } break;
                    case 0x01: {
                        AIL_TODO();
                    } break;
                    case 0x02: {
                        AIL_TODO();
                    } break;
                    case 0x03: { // Sequence/Track Name - ignored
                        u32 len = read_var_len(&buffer);
                        buffer.idx += len;
                    } break;
                    case 0x04: {
                        AIL_TODO();
                    } break;
                    case 0x05: {
                        AIL_TODO();
                    } break;
                    case 0x06: {
                        AIL_TODO();
                    } break;
                    case 0x07: {
                        AIL_TODO();
                    } break;
                    case 0x20: {
                        AIL_TODO();
                    } break;
                    case 0x2f: {
                        // End of Track
                        AIL_ASSERT(ail_buf_read1(&buffer) == 0);
                        AIL_ASSERT(buffer.idx == chunk_end);
                    } break;
                    case 0x51: {
                        // @TODO: This is a change of tempo and should thus be recorded for the track somehow
                        AIL_ASSERT(ail_buf_read1(&buffer) == 3);
                        tempo = ail_buf_read3msb(&buffer);
                        dbg_log("tempo: %d\n", tempo);
                    } break;
                    case 0x54: {
                        AIL_TODO();
                    } break;
                    case 0x58: { // Time Signature - important
                        AIL_ASSERT(ail_buf_read1(&buffer) == 4);
                        time_signature = (MIDI_Time_Signature) {
                            .num    = ail_buf_read1(&buffer),
                            .den    = ail_buf_read1(&buffer),
                            .clocks = ail_buf_read1(&buffer),
                            .b      = ail_buf_read1(&buffer),
                        };
                        dbg_log("time_signature: (%d, %d, %d, %d)\n", time_signature.num, time_signature.den, time_signature.clocks, time_signature.b);
                    } break;
                    case 0x59: {
                        AIL_ASSERT(ail_buf_read1(&buffer) == 2);
                        key_signature = (MIDI_Key_Signature) {
                            .sf = (i8) ail_buf_read1(&buffer),
                            .mi = (i8) ail_buf_read1(&buffer),
                        };
                        dbg_log("key_signature: (%d, %d)\n", key_signature.sf, key_signature.mi);
                    } break;
                    case 0x7f: {
                        AIL_TODO();
                    } break;
                    default: {
                        buffer.idx -= 2;
                        u16 ev = ail_buf_read2msb(&buffer);
                        dbg_log("\033[33mEncountered unknown meta event %#04x.\033[0m\n", ev);
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
                    dbg_log("New Satus - ");
                } else {
                    dbg_log("Running Status - ");
                }
                dbg_log("Command: %#01x, Channel: %#01x\n", command, channel);
                switch (command) {
                    case 0x8:
                    case 0x9: {
                        // Note off / on
                        u8 key      = ail_buf_read1(&buffer);
                        u8 velocity = ail_buf_read1(&buffer);
                        MusicChunk chunk = {
                            .time   = ticks * (u64)(((f32)tempo / (f32)ticksPQN) / 1000.0f),
                            .len    = velocity, // @Study: Is this correct?
                            .key    = key % PIANO_KEY_AMOUNT,
                            .octave = MIDI_0KEY_OCTAVE + (key / PIANO_KEY_AMOUNT),
                            .on     = command == 0x9 && velocity != 0,
                        };
                        u64 chunk_end = chunk.time + chunk.len;
                        if (AIL_LIKELY(chunk_end > val.song.len)) val.song.len = chunk_end;
                        ail_da_push(&val.song.chunks, chunk);
                    } break;
                    case 0xA: {
                        // Polyphonic Key Pressure
                        AIL_TODO();
                    } break;
                    case 0xB: {
                        // Control Change
                        u8 c = ail_buf_read1(&buffer);
                        u8 v = ail_buf_read1(&buffer);
                        dbg_log("Control Change: c = %#01x, v = %#01x\n", c, v);
                        AIL_ASSERT(v <= 127);
                        if (c < 120) {
                            // Do nothing for now
                            // @TODO: Check if any messages here might be interesting for us
                        } else switch (c) {
                            case 120: {
                                // All Sound off
                                AIL_TODO();
                            } break;
                            case 121: {
                                // Reset all controllers
                                // Do nothing
                            } break;
                            case 122: {
                                AIL_TODO();
                            } break;
                            case 123: {
                                // All Notes off
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
                    case 0xC: {
                        // Program Change
                        u8 patch = ail_buf_read1(&buffer);
                        dbg_log("Program Change: patch = %d\n", patch);
                        // Do nothing
                    } break;
                    case 0xD: {
                        // Channel Pressure
                        AIL_TODO();
                    } break;
                    case 0xE: {
                        // Pitch Bend Change.
                        AIL_TODO();
                    } break;
                    case 0xF: {
                        // System Common Messages
                        AIL_TODO();
                    } break;
                }
            }
        }
    }

    return (ParseMidiRes) { true, val };
}

void write_midi(Song song, char *fpath)
{
    AIL_Buffer buffer = ail_buf_new(2048);
    ail_buf_write1(&buffer, 'M');
    ail_buf_write1(&buffer, 'T');
    ail_buf_write1(&buffer, 'h');
    ail_buf_write1(&buffer, 'd');
    ail_buf_write4msb(&buffer, 6);
    ail_buf_write2msb(&buffer, 0);
    ail_buf_write2msb(&buffer, 1);
    ail_buf_write2msb(&buffer, 480);

    ail_buf_write1(&buffer, 'M');
    ail_buf_write1(&buffer, 'T');
    ail_buf_write1(&buffer, 'r');
    ail_buf_write1(&buffer, 'k');
    u64 len_idx = buffer.idx;
    buffer.idx += 4;

    ail_buf_write3msb(&buffer, 0x00ff03); // delta_time + Status for name
    ail_buf_write1(&buffer, strlen(song.name));
    for (u8 i = 0; i < (u8)strlen(song.name); i++) ail_buf_write1(&buffer, song.name[i]);

    ail_buf_write3msb(&buffer, 0x00ff58); // delta_time + Status for Time-Signature
    ail_buf_write1(&buffer, 4);
    ail_buf_write4msb(&buffer, 0x04021808); // Time-Signature

    ail_buf_write3msb(&buffer, 0x00ff59); // delta_time + Status for Key-Signature
    ail_buf_write3msb(&buffer, 0x020000); // len + Key-Signature

    // Tempo
    ail_buf_write3msb(&buffer, 0x00ff51);
    ail_buf_write1(&buffer, 3);
    ail_buf_write3msb(&buffer, 500000);

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

    // Notes
    u64 last_time = 0;
    for (u32 i = 0; i < song.chunks.len; i++) {
        MusicChunk c = song.chunks.data[i];
        u32 delta_time = c.time - last_time;
        do {
            ail_buf_write1(&buffer, delta_time & 0xff);
            delta_time >>= 7;
        } while (delta_time & 0x80);
        last_time = c.time;
        ail_buf_write1(&buffer, c.on ? 0x90 : 0x80);
        ail_buf_write1(&buffer, (c.octave - MIDI_0KEY_OCTAVE)*PIANO_KEY_AMOUNT + c.key);
        ail_buf_write1(&buffer, c.time);
    }

    ail_buf_write4msb(&buffer, 0x01ff2f00);

    u64 cur_idx = buffer.idx;
    buffer.idx  = len_idx;
    ail_buf_write4msb(&buffer, cur_idx - (len_idx + 4));
    buffer.idx  = cur_idx;

    ail_buf_to_file(&buffer, fpath);
}

#endif // _MIDI_IMPL_GUARD_
#endif // MIDI_IMPL
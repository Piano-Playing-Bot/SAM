#include <stdbool.h> // For boolean definitions
#include <stdlib.h>  // For exit, malloc, memcpy
#include <stdio.h>   // For printf - only used for debugging
#define AIL_ALL_IMPL
#include "ail.h"
#define AIL_FS_IMPL
#include "ail_fs.h"
#define AIL_BUF_IMPL
#include "ail_buf.h"
#include "common.h"  // For common type definitions used in this project

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

// Code taken from MIDI Standard
u32 readVarLen(AIL_Buffer *buffer)
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

Song parseMidi(char *filePath)
{
    Song song = {0};
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
        printf("%s is not a midi file\n", fileName);
        exit(1);
    }

    AIL_Buffer buffer = ail_buf_from_file(filePath);
    // Remove file-ending from filename
    nameLen -= EXT_LEN;
    song.name = malloc((nameLen + 1) * sizeof(char));
    memcpy(song.name, fileName, nameLen);
    song.name[nameLen] = 0;

    #define midiFileStartLen 8
    const char midiFileStart[midiFileStartLen] = {'M', 'T', 'h', 'd', 0, 0, 0, 6};

    if (buffer.size < 14 || memcmp(buffer.data, midiFileStart, midiFileStartLen) != 0) {
        printf("Invalid Midi File provided.\nMake sure the File wasn't corrupted\n");
        exit(1);
    }
    buffer.idx += midiFileStartLen;

    u32 tempo; // @TODO: Initialize to 120BPM
    (void)tempo;
    MIDI_Time_Signature timeSignature = { // Initialized to 4/4
        .num = 4,
        .den = 2,
        .clocks = 18,
        .b = 8,
    };
    (void)timeSignature;
    MIDI_Key_Signature keySignature = {0};
    (void)keySignature;

    u16 format   = ail_buf_read2msb(&buffer);
    u16 ntrcks   = ail_buf_read2msb(&buffer);
    u16 division = ail_buf_read2msb(&buffer);
    (void)division;

    printf("format: %d, ntrcks: %d, division: %d\n", format, ntrcks, division);

    if (format > 2) {
        printf("Unknown Midi Format.\nPlease try a different Midi File\n");
        exit(1);
    }

    u8 command = 0; // used in running status (@Note: status == command)
    u8 channel = 0; // used in running status
    for (u16 i = 0; i < ntrcks; i++) {
        // Parse track chunks
        AIL_ASSERT(ail_buf_read4msb(&buffer) == 0x4D54726B);
        u32 chunkLen   = ail_buf_read4msb(&buffer);
        u32 chunkEnd   = buffer.idx + chunkLen;
        printf("Parsing chunk from %#010llx to %#010x\n", buffer.idx, chunkEnd);
        while (buffer.idx < chunkEnd) {
            // Parse MTrk events
            // @TODO: How is the deltaTime used? What does it tell me?
            u32 deltaTime = readVarLen(&buffer);
            printf("deltaTime: %d\n", deltaTime);
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
                        u32 len = readVarLen(&buffer);
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
                        AIL_ASSERT(buffer.idx == chunkEnd);
                    } break;
                    case 0x51: {
                        // @TODO: This is a change of tempo and should thus be recorded for the track somehow
                        AIL_ASSERT(ail_buf_read1(&buffer) == 3);
                        tempo = (ail_buf_read1(&buffer) << 24) | ail_buf_read2msb(&buffer);
                        printf("tempo: %d\n", tempo);
                    } break;
                    case 0x54: {
                        AIL_TODO();
                    } break;
                    case 0x58: { // Time Signature - important
                        AIL_ASSERT(ail_buf_read1(&buffer) == 4);
                        timeSignature = (MIDI_Time_Signature) {
                            .num    = ail_buf_read1(&buffer),
                            .den    = ail_buf_read1(&buffer),
                            .clocks = ail_buf_read1(&buffer),
                            .b      = ail_buf_read1(&buffer),
                        };
                        printf("timeSignature: (%d, %d, %d, %d)\n", timeSignature.num, timeSignature.den, timeSignature.clocks, timeSignature.b);
                    } break;
                    case 0x59: {
                        AIL_ASSERT(ail_buf_read1(&buffer) == 2);
                        keySignature = (MIDI_Key_Signature) {
                            .sf = (i8) ail_buf_read1(&buffer),
                            .mi = (i8) ail_buf_read1(&buffer),
                        };
                        printf("keySignature: (%d, %d)\n", keySignature.sf, keySignature.mi);
                    } break;
                    case 0x7f: {
                        AIL_TODO();
                    } break;
                    default: {
                        buffer.idx -= 2;
                        printf("\033[33mEncountered unknown meta event %#04x.\033[0m\n", ail_buf_read2msb(&buffer));
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
                    printf("New Satus - ");
                } else {
                    printf("Running Status - ");
                }
                printf("Command: %#01x, Channel: %#01x\n", command, channel);
                switch (command) {
                    case 0x8: {
                        // Note off
                        AIL_TODO();
                    } break;
                    case 0x9: {
                        // Note on
                        AIL_TODO();
                    } break;
                    case 0xA: {
                        // Polyphonic Key Pressure
                        AIL_TODO();
                    } break;
                    case 0xB: {
                        // Control Change
                        u8 c = ail_buf_read1(&buffer);
                        u8 v = ail_buf_read1(&buffer);
                        printf("Control Change: c = %#01x, v = %#01x\n", c, v);
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
                        printf("Program Change: patch = %d\n", patch);
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

    return song;
}

int main(void)
{
    Song song = parseMidi("../midis/Empty.mid");

    char *keyStrs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    printf("{\n  name: %s\n  len: %lldms\n  clock: %dms\n  chunks: [\n", song.name, song.len, song.clock);
    for (u32 i = 0; i < song.chunks.len; i++) {
        MusicChunk c = song.chunks.data[i];
        printf("    { key: %2s, octave: %2d, on: %c, time: %lld, len: %d }\n", keyStrs[c.key], c.octave, c.on ? 'y' : 'n', c.time, c.len);
    }
    printf("  ]\n}\n");
    return 0;
}

#include <stdbool.h> // For boolean definitions
#include <stdlib.h>  // For exit, malloc, memcpy
#include <stdio.h>   // For printf - only used for debugging
#define AIL_ALL_IMPL
#include "ail.h"
#define AIL_FS_IMPL
#include "ail_fs.h"
#define AIL_BUF_IMPL
#include "ail_buf.h"

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

typedef struct {
    // @TODO
} MusicChunk;
AIL_DA_INIT(MusicChunk);

typedef struct {
    char *name;
    char *filename;
    u64   len;     // in microseconds
    AIL_DA(MusicChunk) chunks;
} MusicData;

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

MusicData parseMidi(char *filePath)
{
    MusicData musicData = {0};
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

    if (pathLen < 4 || memcmp(&filePath[pathLen - 4], ".mid", 4) != 0) {
        printf("%s is not a midi file\n", fileName);
        exit(1);
    }

    AIL_Buffer buffer = ail_buf_from_file(filePath);
    // Remove file-ending from filename
    musicData.name = malloc((nameLen + 1) * sizeof(char));
    memcpy(musicData.name, fileName, nameLen);
    musicData.name[nameLen] = 0;

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

    if (format > 2) {
        printf("Unknown Midi Format.\nPlease try a different Midi File\n");
        exit(1);
    }

    for (u16 i = 0; i < ntrcks; i++) {
        // Parse track chunks
        u32 chunkLen  = ail_buf_read4msb(&buffer);
        u32 chunkEnd  = buffer.idx + chunkLen;
        while (buffer.idx < chunkEnd) {
            // Parse MTrk events
            // @TODO: How is the deltaTime used? What does it tell me?
            u32 deltaTime = readVarLen(&buffer);
            (void)deltaTime;
                if (ail_buf_peek1(buffer) == 0xff) {
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
                            AIL_TODO();
                        } break;
                        case 0x51: {
                            // @TODO: This is a change of tempo and should thus be recorded for the track somehow
                            if (ail_buf_read1(&buffer) != 3) AIL_UNREACHABLE();
                            tempo = (ail_buf_read1(&buffer) << 24) | ail_buf_read2msb(&buffer);
                        } break;
                        case 0x54: {
                            AIL_TODO();
                        } break;
                        case 0x58: { // Time Signature - important
                            if (ail_buf_read1(&buffer) != 4) AIL_UNREACHABLE();
                            timeSignature = (MIDI_Time_Signature) {
                                .num    = ail_buf_read1(&buffer),
                                .den    = ail_buf_read1(&buffer),
                                .clocks = ail_buf_read1(&buffer),
                                .b      = ail_buf_read1(&buffer),
                            };
                        } break;
                        case 0x59: {
                            if (ail_buf_read1(&buffer) != 2) AIL_UNREACHABLE();
                            keySignature = (MIDI_Key_Signature) {
                                .sf = (i8) ail_buf_read1(&buffer),
                                .mi = (i8) ail_buf_read1(&buffer),
                            };
                        } break;
                        case 0x7f: {
                            AIL_TODO();
                        } break;
                        default: {
                            buffer.idx--;
                            printf("Parsing track event %#04x is not yet implemented.\n", ail_buf_peek2msb(buffer));
                        }
                    }
                }
                else if (ail_buf_peek1(buffer) & 0x80) {
                    u8 status  = (ail_buf_peek1(buffer) & 0xf0) >> 4;
                    u8 channel = ail_buf_read1(&buffer) & 0x0f;
                    (void)channel;
                    switch (status) {
                        default: {
                            AIL_TODO();
                        }
                    }
                }
                else {
                    // MIDI Event or System-Exclusive Event
                    printf("Parsing track event %#02x is not yet implemented.\n", ail_buf_peek1(buffer));
                    AIL_TODO();
                }
        }
    }

    return musicData;
}

int main(void)
{
    parseMidi("../Simplest Test.mid");
    return 0;
}

#ifndef COMMON_H_
#define COMMON_H_

#define AIL_ALL_IMPL
#include "ail.h"

#define DEBUG

// @Note on time: The idea is to use discretized clock-cycles for measuring time.
// The time parameter in MusicChunk represents at which millisecond the chunk should start playing
// The len paramter in MusicChunk represents how many milliseconds the chunk should take to be completed

typedef enum {
    PIANO_KEY_C = 0,
    PIANO_KEY_CS,
    PIANO_KEY_D,
    PIANO_KEY_DS,
    PIANO_KEY_E,
    PIANO_KEY_F,
    PIANO_KEY_FS,
    PIANO_KEY_G,
    PIANO_KEY_GS,
    PIANO_KEY_A,
    PIANO_KEY_AS,
    PIANO_KEY_B,
    PIANO_KEY_AMOUNT,
} PianoKey;

// A MusicChunk represents whether a note should be played or stopped being played, the time at which this should happen, how long the transition from not-playing to playing (or vice versa) should take and which note exactly should be played
typedef struct {
    u64  time;   // The clock-cycle on which to start playing this note
    u16  len;    // The amount of clock cycles, that playing this note should take
    PianoKey  key;    // The note's key
    i8   octave; // Which octave the key should be played on (zero is the middle octave)
    bool on;     // Whether the note should be played (true) or not (false)
} MusicChunk;
AIL_DA_INIT(MusicChunk);

typedef struct {
    char *fname; // Filename that saves the data to this song
    char *name;  // Name of the Song, that is shown in the UI
    u64   len;   // Length in milliseconds of the entire Song
    AIL_DA(MusicChunk) chunks;
} Song;
AIL_DA_INIT(Song);

#endif // COMMON_H_
#ifndef COMMON_H_
#define COMMON_H_

#define AIL_ALL_IMPL
#include "ail.h"

// @Note on time: The idea is to use discretized clock-cycles for measuring time.
// The clock parameter in Song represents the length of each clock-cycle in milliseconds
// The clock parameter in MusicChunk represents on which clock-cycle the chunk should start playing
// The len paramter in MusicChunk represents how many clock-cycles the chunk should take to be completed

typedef enum {
    KEY_C = 0,
    KEY_CS,
    KEY_D,
    KEY_DS,
    KEY_E,
    KEY_F,
    KEY_FS,
    KEY_G,
    KEY_GS,
    KEY_A,
    KEY_AS,
    KEY_B,
} Key;

// A MusicChunk represents whether a note should be played or stopped being played, the time at which this should happen, how long the transition from not-playing to playing (or vice versa) should take and which note exactly should be played
typedef struct {
    u64  time;   // The clock-cycle on which to start playing this note
    u16  len;    // The amount of clock cycles, that playing this note should take
    Key  key;    // The note's key
    i8   octave; // Which octave the key should be played on (zero is the middle octave)
    bool on;     // Whether the note should be played (true) or not (false)
} MusicChunk;
AIL_DA_INIT(MusicChunk);

typedef struct {
    char *name;  // Name of the Song, that is shown in the UI
    u64   len;   // Length in milliseconds of the entire Song
    u32   clock; // Length of a single clock-cycle in milliseconds
    AIL_DA(MusicChunk) chunks;
} Song;

#endif // COMMON_H_
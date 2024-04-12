#ifndef HEADER_H_
#define HEADER_H_

#define AIL_ALL_IMPL
#define AIL_ALLOC_IMPL
#define AIL_MD_IMPL
#define AIL_GUI_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#define AIL_RING_IMPL
#define AIL_SV_IMPL
#define AIL_TIME_IMPL

#include "ail_fs.h"
#include "common.h"
#include "ail.h"
#include "ail_alloc.h"
#include "ail_gui.h"
#include "ail_buf.h"
#include "ail_ring.h"
#include "ail_sv.h"
#include "ail_time.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>  // For calloc, free, memcpy, memcmp

static const CONST_VAR u32 PDIL_MAGIC = (((u32)'P') << 24) | (((u32)'D') << 16) | (((u32)'I') << 8) | (((u32)'L') << 0);

#ifndef DBG_LOG
#ifdef UI_DEBUG
#include <stdio.h> // For printf - only used for debugging
#define DBG_LOG(...) printf(__VA_ARGS__)
#else
#define DBG_LOG(...) do { if (0) printf(__VA_ARGS__); } while(0)
#endif // UI_DEBUG
#endif // DBG_LOG

void print_cmd(PidiCmd c)
{
    static const char *key_strs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    DBG_LOG("{ key: %2s, octave: %2d, dt: %dms, len: %dms, velocity: %d }\n", key_strs[pidi_key(c)], pidi_octave(c), pidi_dt(c), pidi_len(c)*LEN_FACTOR, pidi_velocity(c));
}

void print_song(Song song)
{
    DBG_LOG("{\n  name: %s\n  len: %lldms\n  cmds (%d): [\n", song.name, song.len, song.cmds.len);
    for (u32 i = 0; i < song.cmds.len; i++) {
        DBG_LOG("  ");
        print_cmd(song.cmds.data[i]);
    }
    DBG_LOG("  ]\n}\n");
}

#endif // HEADER_H_
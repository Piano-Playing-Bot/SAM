//////////////
// Includes //
//////////////

#include <stdbool.h> // For boolean definitions
#include <stdio.h>   // For printf - only used for debugging
#include <assert.h>  // For assert
#include <string.h>  // For memcpy
#include "raylib.h"  // For immediate UI framework
#include <pthread.h> // For threads and mutexes
#include <unistd.h>  // For sleep @Cleanup

#define UTIL_MALLOC(size)           alloc_ctxAlloc(size)
#define BUF_MALLOC(size)            alloc_ctxAlloc(size)
#define BUF_FREE(ptr)               alloc_ctxFree(ptr)
#define GUI_MALLOC(size)            alloc_ctxAlloc(size)
#define GUI_FREE(ptr)               alloc_ctxFree(ptr)
#define STBDS_REALLOC(ctx,ptr,size) alloc_ctxRealloc(ptr, size)
#define STBDS_FREE(ctx,ptr)         alloc_ctxFree(ptr)
#define ALLOC_IMPLEMENTATION
#include "alloc.h"
#define UTIL_IMPLEMENTATION
#include "util.h"
#define BUF_IMPLEMENTATION
#include "buf.h"
#define GUI_IMPLEMENTATION
#include "gui.h"
#define SV_IMPLEMENTATION
#include "sv.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"


typedef enum {
    UI_VIEW_START,
    UI_VIEW_FILE,
} UI_View;

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

void *parseFile(void *filePath);
u32 readVarLen(Buffer *buffer);


#define SWITCH_CTX_THREADSAFE(allocator)      { pthread_mutex_lock(&allocCtxMutex); ALLOC_SWITCH_CTX(allocator);      pthread_mutex_unlock(&allocCtxMutex); }
#define SWITCH_BACK_CTX_THREADSAFE(allocator) { pthread_mutex_lock(&allocCtxMutex); ALLOC_SWITCH_BACK_CTX(allocator); pthread_mutex_unlock(&allocCtxMutex); }
pthread_mutex_t allocCtxMutex = PTHREAD_MUTEX_INITIALIZER;
Alloc_Allocator frameArena;
Alloc_Allocator uiStrArena;

// These variables are all accessed by main and parseFile (and the functions called by parseFile)
pthread_mutex_t progMutex = PTHREAD_MUTEX_INITIALIZER;
float prog     = 0.0f; // Always between 0 and 1 or progSucc or progFail
float progSucc = 2.0f;
float progFail = -1.0f;
char *errMsg   = NULL; // Must be set to some message, if prog==progFail
#define SET_PROG(f) { pthread_mutex_lock(&progMutex); prog = (f); pthread_mutex_unlock(&progMutex); }


UI_View view       = UI_VIEW_START;
char *startViewMsg = "Drag-and-Drop a MIDI-File to play it on the Piano";
Gui_Label label    = {0};

// @Cleanup: This is a very bad way of managing memory, using some arena allocator for example would be a much better idea here

int main(void)
{
    frameArena = alloc_arenaInit(alloc_std, 32*1024, true);
    uiStrArena = alloc_arenaInit(alloc_std,  1*1024, false);
    ALLOC_SWITCH_CTX(frameArena);

    i32 winWidth  = 1200;
    i32 winHeight = 600;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winWidth, winHeight, "Piano Player");
    SetTargetFPS(60);

    char *filePath     = NULL;
    pthread_t fileParsingThread;
    i32       fileParsingRet;
    (void)fileParsingRet;

    float size_default = 50;
    float size_max     = size_default;
    Font font = LoadFontEx("./assets/Roboto-Regular.ttf", size_max, NULL, 95);

    Gui_El_Style style_default = {
        .color        = WHITE,
        .bg           = BLANK,
        .border_color = BLANK,
        .border_width = 0,
        .font         = font,
        .font_size    = size_default,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = TEXT_ALIGN_C,
        .vAlign       = TEXT_ALIGN_C,
    };

    label = (Gui_Label) {
        .text = startViewMsg,
        .bounds = (Rectangle){ .x = 0, .y = 0, .width = winWidth, .height = winHeight },
        .defaultStyle = style_default,
        .hovered = style_default,
    };

    while (!WindowShouldClose()) {
        BeginDrawing();

        bool isResized = IsWindowResized();
        if (isResized) {
            winWidth  = GetScreenWidth();
            winHeight = GetScreenHeight();

            label.bounds.width  = winWidth;
            label.bounds.height = winHeight;
        }
        ClearBackground(BLACK);

        switch(view) {
            case UI_VIEW_START: {
                gui_drawLabel(label);

                if (IsFileDropped()) {
                    FilePathList droppedFiles = LoadDroppedFiles();
                    if (droppedFiles.count > 1 || !IsPathFile(droppedFiles.paths[0])) {
                        label.text = "You can only drag and drop one MIDI-File to play it on the Piano.\nPlease try again";
                    } else {
                        view = UI_VIEW_FILE;
                        u64 pathLen = strlen(droppedFiles.paths[0]);
                        filePath    = malloc(sizeof(char) * (pathLen + 1));
                        memcpy(filePath, droppedFiles.paths[0], pathLen + 1);
                        label.text  = filePath;
                        fileParsingRet = pthread_create(&fileParsingThread, NULL, parseFile, (void *)filePath);
                    }
                    UnloadDroppedFiles(droppedFiles);
                }
            } break;

            case UI_VIEW_FILE: {
                float perc;
                pthread_mutex_lock(&progMutex);
                perc = prog;
                pthread_mutex_unlock(&progMutex);

                // @TODO: Animate the progress bar
                if (perc == progFail) {
                    label.text = errMsg;
                    errMsg = NULL;
                    view = UI_VIEW_START;
                } else if (perc == progSucc) {
                    gui_drawSized("Success!!!", (Rectangle){0, 0, winWidth, winHeight}, style_default);
                } else {
                    i32 height = size_default;
                    i32 y      = (winHeight - height)/2.0f;
                    i32 width  = winWidth - 2*style_default.pad;
                    DrawRectangle(style_default.pad, y, width, height, LIGHTGRAY);
                    DrawRectangle(style_default.pad, y, perc*width, height, GREEN);
                }
            } break;
        }

        DrawFPS(10, 10);
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        EndDrawing();
        alloc_arenaFreeAll(frameArena.data);
    }

    CloseWindow();
    return 0;
}

// Code taken from MIDI Standard
u32 readVarLen(Buffer *buffer)
{
    u32 value;
    u8 c;
    if ((value = buf_read1(buffer)) & 0x80) {
        value &= 0x7f;
        do {
            value = (value << 7) + ((c = buf_read1(buffer)) & 0x7f);
        } while (c & 0x80); }
    return value;
}

void *parseFile(void *_filePath)
{
    SET_PROG(0.01f);
    char *filePath = (char *)_filePath;
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
        SWITCH_CTX_THREADSAFE(uiStrArena);
        const char *e1 = "'";
        const char *e2 = "' is not a Midi-File.\nTry again with a Midi-File, please.";
        errMsg = util_memadd(e1, strlen(e1), fileName, nameLen);
        errMsg = util_memadd(errMsg, strlen(e1) + nameLen, e2, strlen(e2));
        label.text = errMsg;
        SWITCH_BACK_CTX_THREADSAFE();
        SET_PROG(progFail);
        return NULL;
    }

    #define midiFileStartLen 8
    const char midiFileStart[midiFileStartLen] = {'M', 'T', 'h', 'd', 0, 0, 0, 6};

    SET_PROG(0.05f);
    // @Performance: Reading the file might take a while.
    // If another thread wanted to switch the allocation context, they would be blocked, until the file is read.
    // This is currently not a problem yet, bc the main thread doesn't switch allocation contexts,
    // while the file is being parsed, but it might become a problem in the future
    SWITCH_CTX_THREADSAFE(alloc_std);
    Buffer buffer = buf_fromFile(filePath);
    SWITCH_BACK_CTX_THREADSAFE();
    SET_PROG(0.1f);

    if (buffer.size < 14 || memcmp(buffer.data, midiFileStart, midiFileStartLen) != 0) {
        errMsg = "Invalid Midi File provided.\nMake sure the File wasn't corrupted.";
        label.text = errMsg;
        SET_PROG(progFail);
        return NULL;
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

    u16 format   = buf_read2msb(&buffer);
    u16 ntrcks   = buf_read2msb(&buffer);
    u16 division = buf_read2msb(&buffer);
    (void)division;

    if (format > 2) {
        errMsg = "Unknown Midi Format.\nPlease try a different Midi File.";
        label.text = errMsg;
        SET_PROG(progFail);
        return NULL;
    }

    for (u16 i = 0; i < ntrcks; i++) {
        // Parse track chunks
        SET_PROG(UTIL_MIN(0.9f, prog + ((float)i/(float)ntrcks)));
        u32 chunkLen  = buf_read4msb(&buffer);
        u32 chunkEnd  = buffer.idx + chunkLen;
        while (buffer.idx < chunkEnd) {
            // Parse MTrk events
            SET_PROG(prog + (float)buffer.idx/((float)ntrcks*(float)chunkEnd));
            // @TODO: How is the deltaTime used? What does it tell me?
            u32 deltaTime = readVarLen(&buffer);
            (void)deltaTime;
                if (buf_peek1(buffer) == 0xff) {
                    // Meta Event
                    switch (buf_read1(&buffer)) {
                        case 0x00: {
                            UTIL_TODO();
                        } break;
                        case 0x01: {
                            UTIL_TODO();
                        } break;
                        case 0x02: {
                            UTIL_TODO();
                        } break;
                        case 0x03: { // Sequence/Track Name - ignored
                            u32 len = readVarLen(&buffer);
                            buffer.idx += len;
                        } break;
                        case 0x04: {
                            UTIL_TODO();
                        } break;
                        case 0x05: {
                            UTIL_TODO();
                        } break;
                        case 0x06: {
                            UTIL_TODO();
                        } break;
                        case 0x07: {
                            UTIL_TODO();
                        } break;
                        case 0x20: {
                            UTIL_TODO();
                        } break;
                        case 0x2f: {
                            UTIL_TODO();
                        } break;
                        case 0x51: {
                            // @UTIL_TODO: This is a change of tempo and should thus be recorded for the track somehow
                            if (buf_read1(&buffer) != 3) UTIL_UNREACHABLE();
                            tempo = (buf_read1(&buffer) << 24) | buf_read2msb(&buffer);
                        } break;
                        case 0x54: {
                            UTIL_TODO();
                        } break;
                        case 0x58: { // Time Signature - important
                            if (buf_read1(&buffer) != 4) UTIL_UNREACHABLE();
                            timeSignature = (MIDI_Time_Signature) {
                                .num    = buf_read1(&buffer),
                                .den    = buf_read1(&buffer),
                                .clocks = buf_read1(&buffer),
                                .b      = buf_read1(&buffer),
                            };
                        } break;
                        case 0x59: {
                            if (buf_read1(&buffer) != 2) UTIL_UNREACHABLE();
                            keySignature = (MIDI_Key_Signature) {
                                .sf = (i8) buf_read1(&buffer),
                                .mi = (i8) buf_read1(&buffer),
                            };
                        } break;
                        case 0x7f: {
                            UTIL_TODO();
                        } break;
                        default: {
                            buffer.idx--;
                            printf("Parsing track event %#04x is not yet implemented.\n", buf_peek2msb(buffer));
                        }
                    }
                }
                else if (buf_peek1(buffer) & 0x80) {
                    u8 status  = (buf_peek1(buffer) & 0xf0) >> 4;
                    u8 channel = buf_read1(&buffer) & 0x0f;
                    (void)channel;
                    switch (status) {
                        default: {
                            UTIL_TODO();
                        }
                    }
                }
                else {
                    // MIDI Event or System-Exclusive Event
                    printf("Parsing track event %#02x is not yet implemented.\n", buf_peek1(buffer));
                    UTIL_TODO();
                }
        }
    }

    return NULL;
}
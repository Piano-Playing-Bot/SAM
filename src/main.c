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
#define GUI_MALLOC(size)            alloc_ctxAlloc(size)
#define GUI_FREE(ptr)               alloc_ctxFree(ptr)
#define STBDS_REALLOC(ctx,ptr,size) alloc_ctxRealloc(ptr, size)
#define STBDS_FREE(ctx,ptr)         alloc_ctxFree(ptr)
#define ALLOC_IMPLEMENTATION
#include "alloc.h"
#define UTIL_IMPLEMENTATION
#include "util.h"
#define GUI_IMPLEMENTATION
#include "gui.h"
#define SV_IMPLEMENTATION
#include "sv.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"


#define MSB_U16(bytes) ((bytes)[0] << 8) | (bytes)[1]

typedef enum {
    UI_VIEW_START,
    UI_VIEW_FILE,
} UI_View;

void *parseFile(void *filePath);


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
#define SET_PROG(f) { pthread_mutex_lock(&progMutex); prog = f; pthread_mutex_unlock(&progMutex); }


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

void *parseFile(void *_filePath)
{
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

    SWITCH_CTX_THREADSAFE(alloc_std);
    u64 fileSize;
    char *file = util_readFile(filePath, &fileSize);
    SWITCH_BACK_CTX_THREADSAFE();

    if (fileSize < 14 || memcmp(file, midiFileStart, midiFileStartLen) != 0) {
        errMsg = "Invalid Midi File provided.\nMake sure the File wasn't corrupted.";
        label.text = errMsg;
        SET_PROG(progFail);
        return NULL;
    }

    u16 format   = MSB_U16(&file[midiFileStartLen]);
    u16 ntrcks   = MSB_U16(&file[midiFileStartLen + 2]);
    u16 division = MSB_U16(&file[midiFileStartLen + 4]);
    (void)ntrcks;
    (void)division;

    if (format > 2) {
        errMsg = "Unknown Midi Format.\nPlease try a different Midi File.";
        label.text = errMsg;
        SET_PROG(progFail);
        return NULL;
    }

    for (float f = 0.0f; f < 1.0f; f += 0.056263f)
    {
        SET_PROG(f);
        sleep(1);
    }
    SET_PROG(progSucc);
    return NULL;
}
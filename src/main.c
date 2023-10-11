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

#define UTIL_IMPLEMENTATION
#include "util.h"
#define GUI_IMPLEMENTATION
#include "gui.h"
#define SV_IMPLEMENTATION
#include "sv.h"
#define STB_DS_IMPLEMENTATION
#define STBDS_NO_SHORT_NAMES
#include "stb_ds.h"  // For dynamic arrays


typedef enum {
    UI_VIEW_START,
    UI_VIEW_FILE,
} UI_View;

void *parseFile(void *filePath);

// These variables are all accessed by main and parseFile (and the functions called by parseFile)
pthread_mutex_t progMutex = PTHREAD_MUTEX_INITIALIZER;
float prog = 0.0f; // Always between 0 and 1 or progSucc or progFail
float progSucc = 2.0f;
float progFail = -1.0f;
pthread_mutex_t msgIdxMutex = PTHREAD_MUTEX_INITIALIZER;
i32 msgIdx = 0;

int main(void)
{
    i32 winWidth  = 1200;
    i32 winHeight = 600;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winWidth, winHeight, "Piano Player");
    SetTargetFPS(60);

    UI_View view       = UI_VIEW_START;
    char *startViewMsg = "Drag-and-Drop a MIDI-File to play it on the Piano";
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

    Gui_Label label = {
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
                    gui_drawSized("Failure in parsing provided file", (Rectangle){0, 0, winWidth, winHeight}, style_default);
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
    }

    CloseWindow();
    return 0;
}

void *parseFile(void *filePath)
{
    (void)filePath;
    for (float f = 0.0f; f < 1.0f; f += 0.056263f)
    {
        pthread_mutex_lock(&progMutex);
        prog = f;
        pthread_mutex_unlock(&progMutex);
        sleep(1);
    }
    pthread_mutex_lock(&progMutex);
    prog = progSucc;
    pthread_mutex_unlock(&progMutex);
    return NULL;
}
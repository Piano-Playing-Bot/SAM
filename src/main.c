//////////////
// Includes //
//////////////

#include <stdbool.h>   // For boolean definitions
#include <stdio.h>     // For printf - only used for debugging
#include <assert.h>    // For assert
#include <string.h>    // For memcpy
#include "raylib.h"    // For immediate UI framework

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

int main(void)
{
    i32 winWidth  = 1200;
    i32 winHeight = 600;

    UI_View view = UI_VIEW_START;
    char *startViewMsg = "Drag-and-Drop a MIDI-File to play it on the Piano";
    char *filePath = NULL;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winWidth, winHeight, "RL");
    SetTargetFPS(60);

    float spacing      = 2;
    float padding      = 5;
    float margin       = 8;
    float size_default = 50;
    float size_max     = size_default;
    Font font = LoadFontEx("./assets/Roboto-Regular.ttf", size_max, NULL, 95);

    while (!WindowShouldClose()) {
        BeginDrawing();

        bool isResized = IsWindowResized();
        if (isResized) {
            winWidth  = GetScreenWidth();
            winHeight = GetScreenHeight();
        }
        ClearBackground(BLACK);

        switch(view) {
            case UI_VIEW_START: {
                Vector2 textSize = MeasureTextEx(font, startViewMsg, size_default, spacing);
                Vector2 textPos  = { .x = (winWidth - textSize.x)/2.0f, .y = (winHeight - textSize.y)/2.0f };
                DrawTextEx(font, startViewMsg, textPos, size_default, spacing, WHITE);

                if (IsFileDropped()) {
                    FilePathList droppedFiles = LoadDroppedFiles();
                    if (droppedFiles.count > 1 || !IsPathFile(droppedFiles.paths[0])) {
                        startViewMsg = "You can only drag and drop one MIDI-File to play it on the Piano.\nPlease try again";
                    } else {
                        view = UI_VIEW_FILE;
                        u64 pathLen = strlen(droppedFiles.paths[0]);
                        filePath = malloc(sizeof(char) * (pathLen + 1));
                        memcpy(filePath, droppedFiles.paths[0], pathLen + 1);
                    }
                    UnloadDroppedFiles(droppedFiles);
                }
            } break;

            case UI_VIEW_FILE: {
                Vector2 textSize = MeasureTextEx(font, filePath, size_default, spacing);
                Vector2 textPos  = { .x = (winWidth - textSize.x)/2.0f, .y = (winHeight - textSize.y)/2.0f };
                DrawTextEx(font, filePath, textPos, size_default, spacing, WHITE);
            } break;
        }

        // DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
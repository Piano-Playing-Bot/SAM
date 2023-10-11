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

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winWidth, winHeight, "Piano Player");
    SetTargetFPS(60);

    UI_View view       = UI_VIEW_START;
    char *startViewMsg = "Drag-and-Drop a MIDI-File to play it on the Piano";
    char *filePath     = NULL;

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
                    }
                    UnloadDroppedFiles(droppedFiles);
                }
            } break;

            case UI_VIEW_FILE: {
                gui_drawLabel(label);
            } break;
        }

        DrawFPS(10, 10);
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
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

#define AIL_ALL_IMPL
#include "ail.h"
#define AIL_GUI_IMPL
#include "ail_gui.h"
#define AIL_FS_IMPL
#include "ail_fs.h"
#define AIL_BUF_IMPL
#include "ail_buf.h"
#define AIL_ALLOC_IMPL
#include "ail_alloc.h"


u64 LIBRARY_FILE_MAGIC = ((u64)'M' << 56) | ((u64)'u' << 48) | ((u64)'s' << 40) | ((u64)'i' << 32) |
                         ((u64)'c' << 24) | ((u64)'L' << 16) | ((u64)'i' <<  8) | ((u64)'b' <<  0);

typedef enum {
    UI_VIEW_START,
    UI_VIEW_FILE,
} UI_View;

typedef struct {
    // @TODO
} MusicChunk;
AIL_DA_INIT(MusicChunk);

typedef struct {
    char *name;
    char *filename;
    u64   len;     // in microseconds
    AIL_DA(MusicChunk) data;
} MusicData;
AIL_DA_INIT(MusicData);

void *loadLibrary(void *arg);
void *parseFile(void *_filePath);
void *util_memadd(const void *a, u64 a_size, const void *b, u64 b_size);


#define SWITCH_CTX_THREADSAFE(allocator)      do { pthread_mutex_lock(&allocCtxMutex); AIL_ALLOC_SWITCH_CTX(allocator);      pthread_mutex_unlock(&allocCtxMutex); } while(0)
#define SWITCH_BACK_CTX_THREADSAFE(allocator) do { pthread_mutex_lock(&allocCtxMutex); AIL_ALLOC_SWITCH_BACK_CTX(allocator); pthread_mutex_unlock(&allocCtxMutex); } while(0)
pthread_mutex_t allocCtxMutex = PTHREAD_MUTEX_INITIALIZER;
AIL_Alloc_Allocator frameArena;
AIL_Alloc_Allocator uiStrArena;

// These variables are all accessed by main and parseFile (and the functions called by parseFile)
pthread_mutex_t progMutex = PTHREAD_MUTEX_INITIALIZER;
float prog     = 0.0f; // Always between 0 and 1 or progSucc or progFail
float progSucc = 2.0f;
float progFail = -1.0f;
char *errMsg   = NULL; // Must be set to some message, if prog==progFail
#define SET_PROG(f) { pthread_mutex_lock(&progMutex); prog = (f); pthread_mutex_unlock(&progMutex); }

// These variables are all accessed by main and loadLibrary
AIL_DA(MusicData) library;
bool libraryReady  = false;


UI_View view       = UI_VIEW_START;
char *startViewMsg = "Drag-and-Drop a MIDI-File to play it on the Piano";
AIL_Gui_Label label    = {0};


int main(void)
{
    frameArena = ail_alloc_arena_init(ail_alloc_std, 32*1024, true);
    uiStrArena = ail_alloc_arena_init(ail_alloc_std,  1*1024, false);
    AIL_ALLOC_SWITCH_CTX(frameArena);

    i32 winWidth  = 1200;
    i32 winHeight = 600;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winWidth, winHeight, "Piano Player");
    SetTargetFPS(60);

    char *filePath     = NULL;
    pthread_t fileParsingThread;
    pthread_t loadLibraryThread;

    pthread_create(&loadLibraryThread, NULL, loadLibrary, NULL);

    float size_default = 50;
    float size_max     = size_default;
    Font font = LoadFontEx("./assets/Roboto-Regular.ttf", size_max, NULL, 95);

    AIL_Gui_Style style_default = {
        .color        = WHITE,
        .bg           = BLANK,
        .border_color = BLANK,
        .border_width = 0,
        .font         = font,
        .font_size    = size_default,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = AIL_GUI_ALIGN_C,
        .vAlign       = AIL_GUI_ALIGN_C,
    };

    label = (AIL_Gui_Label) {
        .text = ail_da_from_parts(char, startViewMsg, strlen(startViewMsg), strlen(startViewMsg)),
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
                if (!libraryReady) {
                    label.text.data = "Loading Libary...";
                    ail_gui_drawLabel(label);
                }
                else {
                    ail_gui_drawLabel(label);
                    if (IsFileDropped()) {
                        FilePathList droppedFiles = LoadDroppedFiles();
                        if (droppedFiles.count > 1 || !IsPathFile(droppedFiles.paths[0])) {
                            label.text.data = "You can only drag and drop one MIDI-File to play it on the Piano.\nPlease try again";
                        } else {
                            view = UI_VIEW_FILE;
                            u64 pathLen = strlen(droppedFiles.paths[0]);
                            filePath    = malloc(sizeof(char) * (pathLen + 1));
                            memcpy(filePath, droppedFiles.paths[0], pathLen + 1);
                            label.text.data  = filePath;
                            pthread_create(&fileParsingThread, NULL, parseFile, (void *)filePath);
                        }
                        UnloadDroppedFiles(droppedFiles);
                    }
                }
            } break;

            case UI_VIEW_FILE: {
                float perc;
                pthread_mutex_lock(&progMutex);
                perc = prog;
                pthread_mutex_unlock(&progMutex);

                // @TODO: Animate the progress bar
                if (perc == progFail) {
                    label.text.data = errMsg;
                    errMsg = NULL;
                    view = UI_VIEW_START;
                } else if (perc == progSucc) {
                    ail_gui_drawSized("Success!!!", (Rectangle){0, 0, winWidth, winHeight}, style_default);
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
        ail_alloc_arena_free_all(frameArena.data);
    }

    CloseWindow();
    return 0;
}

// Returns a new array, that contains first array a and then array b. Useful for adding strings for example
// a_size and b_size should both be the size in bytes, not the count of elements
void* util_memadd(const void *a, u64 a_size, const void *b, u64 b_size)
{
	char* out = malloc(a_size * b_size);
	memcpy(out, a, a_size);
	memcpy(&out[a_size], b, b_size);
	return (void*) out;
}

void *loadLibrary(void *arg)
{
    (void)arg;
    library = ail_da_new(MusicData);
    SWITCH_CTX_THREADSAFE(ail_alloc_std);
    const char *dataDir = "./data/";
    const char *libraryFilename = "library";
    char *libraryFilepath = util_memadd(dataDir, strlen(dataDir), libraryFilename, strlen(libraryFilename) + 1);
    SWITCH_BACK_CTX_THREADSAFE();

    if (!DirectoryExists(dataDir)) {
        mkdir(dataDir);
        goto nothing_to_load;
    }
    else {
        if (!FileExists(libraryFilepath)) goto nothing_to_load;
        AIL_Buffer buf = ail_buf_from_file(libraryFilepath);
        if (ail_buf_read8msb(&buf) != LIBRARY_FILE_MAGIC) goto nothing_to_load;
        u32 n = ail_buf_read4lsb(&buf);
        library.cap = n;
        for (; n > 0; n--) {
            u32 nameLen  = ail_buf_read4lsb(&buf);
            u32 fnameLen = ail_buf_read4lsb(&buf);
            (void)fnameLen;
            (void)nameLen;
            SWITCH_CTX_THREADSAFE(ail_alloc_std);

            SWITCH_BACK_CTX_THREADSAFE();
        }
        goto end;
    }

nothing_to_load:
    library.cap = 16;
    libraryReady = true;
end:
    SWITCH_CTX_THREADSAFE(ail_alloc_std);
    free(libraryFilepath);
    SWITCH_BACK_CTX_THREADSAFE();
    return NULL;
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
        label.text.data = errMsg;
        SWITCH_BACK_CTX_THREADSAFE();
        SET_PROG(progFail);
        return NULL;
    }

    // @TODO: Call parseMidi()
    return NULL;
}
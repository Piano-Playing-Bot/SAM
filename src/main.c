//////////////
// Includes //
//////////////

#include "raylib.h"  // For immediate UI framework
#include <stdbool.h> // For boolean definitions
#include <string.h>  // For memcpy
#include <pthread.h> // For threads and mutexes
#include <unistd.h>  // For sleep @Cleanup
#include "math.h"    // For sinf, cosf
#include "common.h"
#define MIDI_IMPL
#include "midi.h"
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

// PIDI = Piano Digital Interface
// PDIL = PIDI-Library
u32 PIDI_MAGIC = ('P' << 24) | ('I' << 16) | ('D' << 8) | ('I' << 0);
u32 PDIL_MAGIC = ('P' << 24) | ('D' << 16) | ('I' << 8) | ('L' << 0);

const char *data_dir_path    = "/data/";
const char *library_filepath = "/data/library.pdil";

#define FPS 60

typedef enum {
    UI_VIEW_LIBRARY,
    UI_VIEW_ADD,
    UI_VIEW_PARSING_SONG,
} UI_View;

void  print_song(Song song);
bool  save_pidi(Song song);
bool  save_library();
void *load_library(void *arg);
void *parse_file(void *_filePath);
void *util_memadd(const void *a, u64 a_size, const void *b, u64 b_size);


#define SWITCH_CTX_THREADSAFE(allocator)      do { pthread_mutex_lock(&allocCtxMutex); AIL_ALLOC_SWITCH_CTX(allocator);      pthread_mutex_unlock(&allocCtxMutex); } while(0)
#define SWITCH_BACK_CTX_THREADSAFE(allocator) do { pthread_mutex_lock(&allocCtxMutex); AIL_ALLOC_SWITCH_BACK_CTX(allocator); pthread_mutex_unlock(&allocCtxMutex); } while(0)
pthread_mutex_t allocCtxMutex = PTHREAD_MUTEX_INITIALIZER;
AIL_Alloc_Allocator frame_arena;
AIL_Alloc_Allocator uiStrArena;

// These variables are all accessed by main and parse_file (and the functions called by parse_file)
static bool file_parsed;
static char *err_msg;

// These variables are all accessed by main and load_library
// @TODO: Make library into a Trie to simplify search & prevent duplicates
AIL_DA(Song) library;
bool library_ready = false;


UI_View view = UI_VIEW_LIBRARY;


int main(void)
{
    frame_arena = ail_alloc_arena_init(ail_alloc_std, 32*1024, true);
    uiStrArena = ail_alloc_arena_init(ail_alloc_std,  1*1024, false);
    AIL_ALLOC_SWITCH_CTX(frame_arena);

    i32 win_width  = 1200;
    i32 win_height = 600;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(win_width, win_height, "Piano Player");
    SetTargetFPS(FPS);

    char *file_path     = NULL;
    pthread_t fileParsingThread;
    pthread_t loadLibraryThread;

    pthread_create(&loadLibraryThread, NULL, load_library, NULL);

    f32 size_smaller = 35;
    f32 size_default = 50;
    f32 size_max     = size_default;
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
    AIL_Gui_Style style_default_lt = ail_gui_cloneStyle(style_default);
    style_default_lt.hAlign = AIL_GUI_ALIGN_LT;
    style_default_lt.vAlign = AIL_GUI_ALIGN_LT;
    AIL_Gui_Style style_button_default = {
        .color        = WHITE,
        .bg           = (Color){42, 230, 37, 255},
        .border_color = (Color){ 9, 170,  6, 255},
        .border_width = 5,
        .font         = font,
        .font_size    = size_default,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = AIL_GUI_ALIGN_C,
        .vAlign       = AIL_GUI_ALIGN_C,
    };
    AIL_Gui_Style style_button_hover = ail_gui_cloneStyle(style_button_default);
    style_button_hover.border_color = BLACK;
    AIL_Gui_Style style_song_name_default = {
        .color        = WHITE,
        .bg           = LIGHTGRAY,
        .border_color = GRAY,
        .font         = font,
        .border_width = 5,
        .font_size    = size_smaller,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = AIL_GUI_ALIGN_LT,
        .vAlign       = AIL_GUI_ALIGN_LT,
    };
    AIL_Gui_Style style_song_name_hover = ail_gui_cloneStyle(style_song_name_default);
    style_song_name_hover.bg = GRAY;

    Rectangle header_bounds, content_bounds;
    AIL_Gui_Label centered_label = {
        .text         = ail_da_new_empty(char),
        .defaultStyle = style_default,
        .hovered      = style_default,
    };
    char *library_label_msg = "Library:";
    AIL_Gui_Label library_label = {
        .text         = ail_da_from_parts(char, library_label_msg, strlen(library_label_msg), strlen(library_label_msg)),
        .defaultStyle = style_default_lt,
        .hovered      = style_default_lt,
    };
    char *upload_btn_msg = "Add";
    AIL_Gui_Label upload_button = {
        .text         = ail_da_from_parts(char, upload_btn_msg, strlen(upload_btn_msg), strlen(upload_btn_msg)),
        .defaultStyle = style_button_default,
        .hovered      = style_button_hover,
    };

#define SET_VIEW(v) do { view = (v); view_changed = true; } while(0)
    bool is_first_frame = true;
    bool view_changed   = false;
    while (!WindowShouldClose()) {
        BeginDrawing();

        bool is_resized = IsWindowResized() || is_first_frame;
        is_first_frame = false;
        if (is_resized) {
            win_width  = GetScreenWidth();
            win_height = GetScreenHeight();
            u32 header_pad = 5;
            header_bounds  = (Rectangle) { header_pad, 0, win_width - 2*header_pad, AIL_CLAMP(win_height / 10, size_max + 30, size_max*2) };
            content_bounds = (Rectangle) { header_bounds.x, header_bounds.y + header_bounds.height, header_bounds.width, win_height - header_bounds.y - header_bounds.height };
        }
        ClearBackground(BLACK);
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        static f32 scroll = 0;
        if (view_changed) scroll = 0.0f;
        f32 scroll_delta = -GetMouseWheelMove();
        scroll += scroll_delta;
        if (AIL_UNLIKELY(scroll < 0.0f)) scroll = 0.0f;

        switch(view) {
            case UI_VIEW_LIBRARY: {
                if (is_resized || view_changed) {
                    centered_label.bounds = (Rectangle){0, 0, win_width, win_height};
                    library_label.bounds  = header_bounds;
                    u32     upload_margin   = 5;
                    Vector2 upload_txt_size = MeasureTextEx(upload_button.defaultStyle.font, upload_button.text.data, upload_button.defaultStyle.font_size, upload_button.defaultStyle.cSpacing);
                    upload_button.bounds.y      = header_bounds.y + upload_margin + upload_button.defaultStyle.border_width;
                    upload_button.bounds.height = header_bounds.height - upload_button.bounds.y - upload_button.defaultStyle.border_width - upload_margin;
                    upload_button.bounds.width  = upload_button.defaultStyle.border_width*2 + upload_button.defaultStyle.pad*2 + upload_txt_size.x;
                    upload_button.bounds.x      = header_bounds.x + header_bounds.width - upload_margin - upload_button.bounds.width;
                }
                if (!library_ready) {
                    char *loading_lib_text = "Loading Libary...";
                    centered_label.text    = ail_da_from_parts(char, loading_lib_text, strlen(loading_lib_text), strlen(loading_lib_text));
                    ail_gui_drawLabel(centered_label);
                }
                else {
                    DrawRectangle(header_bounds.x, header_bounds.y, header_bounds.width, header_bounds.height, LIGHTGRAY);
                    AIL_Gui_Drawable_Text library_label_drawable = ail_gui_prepTextForDrawing(library_label.text.data, library_label.bounds, library_label.defaultStyle);
                    ail_gui_drawPreparedSized(library_label_drawable, library_label.bounds, library_label.defaultStyle);

                    AIL_Gui_State upload_button_state = ail_gui_drawLabel(upload_button);
                    if (upload_button_state == AIL_GUI_STATE_PRESSED) SET_VIEW(UI_VIEW_ADD);

                    // @TODO: Allow scrolling
                    // @TODO: Add search bar
                    static const u32 song_name_width  = 200;
                    static const u32 song_name_height = 150;
                    static const u32 song_name_margin = 50;
                    u32 full_song_name_width  = song_name_width  + 2*style_song_name_default.pad + 2*style_song_name_default.border_width;
                    u32 full_song_name_height = song_name_height + 2*style_song_name_default.pad + 2*style_song_name_default.border_width;
                    u32 song_names_per_row    = (content_bounds.width + song_name_margin) / (song_name_margin + full_song_name_width);
                    u32 rows_amount           = (library.len / song_names_per_row) + ((library.len % song_names_per_row) > 0);
                    u32 full_width            = song_names_per_row*full_song_name_width + (song_names_per_row - 1)*song_name_margin;
                    u32 start_x               = (content_bounds.width - full_width) / 2;
                    u32 virtual_height        = rows_amount*full_song_name_height + (rows_amount - 1)*song_name_margin;
                    u32 max_y                 = (virtual_height > content_bounds.height) ? virtual_height - content_bounds.height : 0;
                    scroll = AIL_MAX(scroll, ((f32) max_y));
                    u32 start_row             = scroll / (full_song_name_height + song_name_margin);

                    for (u32 i = start_row * song_names_per_row; i < library.len; i++) {
                        Rectangle song_bounds = {
                            start_x + (full_song_name_width + song_name_margin)*(i % song_names_per_row),
                            content_bounds.y + song_name_margin + (full_song_name_height + song_name_margin)*(i / song_names_per_row) - scroll,
                            song_name_width,
                            song_name_height
                        };
                        char *song_name = library.data[i].name;
                        AIL_Gui_Label song_label = {
                            .text         = ail_da_from_parts(char, song_name, strlen(song_name), strlen(song_name)),
                            .bounds       = song_bounds,
                            .defaultStyle = style_song_name_default,
                            .hovered      = style_song_name_hover,
                        };
                        AIL_Gui_State song_label_state = ail_gui_drawLabelOuterBounds(song_label, content_bounds);
                        if (song_label_state == AIL_GUI_STATE_PRESSED) {
                            printf("Playing song: %s\n", song_name);
                            // @TODO: Send song to arduino & change view
                        }
                    }
                }
            } break;

            case UI_VIEW_ADD: {
                char *add_view_msg  = "Drag-and-Drop a MIDI-File to play it on the Piano";
                centered_label.text = ail_da_from_parts(char, add_view_msg, strlen(add_view_msg), strlen(add_view_msg));

                ail_gui_drawLabel(centered_label);
                if (IsFileDropped()) {
                    FilePathList dropped_files = LoadDroppedFiles();
                    if (dropped_files.count > 1 || !IsPathFile(dropped_files.paths[0])) {
                        centered_label.text.data = "You can only drag and drop one MIDI-File to play it on the Piano.\nPlease try again";
                    } else {
                        SET_VIEW(UI_VIEW_PARSING_SONG);
                        u64 pathLen = strlen(dropped_files.paths[0]);
                        file_path    = malloc(sizeof(char) * (pathLen + 1));
                        memcpy(file_path, dropped_files.paths[0], pathLen + 1);
                        centered_label.text.data  = file_path;
                        pthread_create(&fileParsingThread, NULL, parse_file, (void *)file_path);
                    }
                    UnloadDroppedFiles(dropped_files);
                }
            } break;

            case UI_VIEW_PARSING_SONG: {
                static       u32 parsing_song_anim_idx             = 0;
                static const u32 parsing_song_anim_len             = FPS;
                static const u8  parsing_song_anim_circle_count    = 12;
                static const f32 parsing_song_anim_circle_radius   = 20;
                static const f32 parsing_song_anim_circle_distance = 100;
                u8 parsing_song_cur_song = (u8)(((f32) parsing_song_anim_circle_count * parsing_song_anim_idx) / (f32)parsing_song_anim_len);
                for (u8 i = 0; i < parsing_song_anim_circle_count; i++) {
                    f32 i_perc = i / (f32)parsing_song_anim_circle_count;
                    u32 x      = win_width/2  + parsing_song_anim_circle_distance*cosf(2*PI*i_perc);
                    u32 y      = win_height/2 + parsing_song_anim_circle_distance*sinf(2*PI*i_perc);
                    f32 delta  = (i + parsing_song_anim_circle_count - parsing_song_cur_song) / (f32)parsing_song_anim_circle_count;
                    Color col = {0xff, 0xff, 0xff, 0xff*delta};
                    DrawCircle(x, y, parsing_song_anim_circle_radius, col);
                }
                parsing_song_anim_idx = (parsing_song_anim_idx + 1) % parsing_song_anim_len;

                if (file_parsed) SET_VIEW(UI_VIEW_LIBRARY);
            } break;
        }

        DrawFPS(win_width - 90, 10);
        EndDrawing();
        ail_alloc_arena_free_all(frame_arena.data);
    }

    CloseWindow();
    return 0;
}

void print_song(Song song)
{
    char *key_strs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    printf("{\n  name: %s\n  len: %lldms\n  chunks: [\n", song.name, song.len);
    for (u32 i = 0; i < song.chunks.len; i++) {
        MusicChunk c = song.chunks.data[i];
        printf("    { key: %2s, octave: %2d, on: %c, time: %lld, len: %d }\n", key_strs[c.key], c.octave, c.on ? 'y' : 'n', c.time, c.len);
    }
    printf("  ]\n}\n");
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

bool save_pidi(Song song)
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write8msb(&buf, PIDI_MAGIC);
    ail_buf_write4lsb(&buf, song.chunks.len);
    for (u32 i = 0; i < song.chunks.len; i++) {
        MusicChunk chunk = song.chunks.data[i];
        ail_buf_write8lsb(&buf, chunk.time);
        ail_buf_write2lsb(&buf, chunk.len);
        ail_buf_write1   (&buf, chunk.key);
        ail_buf_write1   (&buf, (u8) chunk.octave);
        ail_buf_write1   (&buf, (u8) chunk.on);
    }
    return ail_buf_to_file(&buf, song.fname);
}

bool save_library()
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write8msb(&buf, PDIL_MAGIC);
    ail_buf_write4lsb(&buf, library.len);
    for (u32 i = 0; i < library.len; i++) {
        Song song = library.data[i];
        u32 name_len  = strlen(song.name);
        u32 fname_len = strlen(song.fname);
        ail_buf_write4lsb(&buf, name_len);
        ail_buf_write4lsb(&buf, fname_len);
        ail_buf_write8lsb(&buf, song.len);
        ail_buf_writestr(&buf, song.name, name_len);
        ail_buf_writestr(&buf, song.fname, fname_len);
    }
    if (!DirectoryExists(data_dir_path)) mkdir(data_dir_path);
    return ail_buf_to_file(&buf, library_filepath);
}

void *load_library(void *arg)
{
    (void)arg;
    library = ail_da_new(Song);
    if (!DirectoryExists(data_dir_path)) {
        mkdir(data_dir_path);
        goto nothing_to_load;
    }
    else {
        if (!FileExists(library_filepath)) goto nothing_to_load;
        AIL_Buffer buf = ail_buf_from_file(library_filepath);
        if (ail_buf_read8msb(&buf) != PDIL_MAGIC) goto nothing_to_load;
        u32 n = ail_buf_read4lsb(&buf);
        ail_da_maybe_grow(&library, n);
        for (; n > 0; n--) {
            u32 name_len  = ail_buf_read4lsb(&buf);
            u32 fname_len = ail_buf_read4lsb(&buf);
            u64 len       = ail_buf_read8lsb(&buf);
            char *name  = ail_buf_readstr(&buf, name_len);
            char *fname = ail_buf_readstr(&buf, fname_len);
            Song song = {
                .fname  = fname,
                .name   = name,
                .len    = len,
                .chunks = ail_da_new_empty(MusicChunk),
            };
            ail_da_push(&library, song);
        }
        goto end;
    }

nothing_to_load:
    ail_da_maybe_grow(&library, 16);
end:
    library_ready = true;
    return NULL;
}

void *parse_file(void *_filePath)
{
    ParseMidiRes res = parse_midi((char *)_filePath);
    err_msg = NULL;
    if (res.succ) {
        print_song(res.val.song);
        static const char *ext   = ".pidi";
        static const u64 ext_len = 5;
        u32 name_len = strlen(res.val.song.name);
        char *fname  = malloc(name_len + ext_len + 1);
        memcpy(fname, res.val.song.name, name_len);
        memcpy(&fname[name_len], ext, ext_len + 1);
        printf("fname: %s\n", fname);
        res.val.song.fname = fname;
        // @TODO: What if file with that name already exists?

        ail_da_push(&library, res.val.song);
        if (!save_pidi(res.val.song)) AIL_TODO();
        if (!save_library()) AIL_TODO();
    }
    else {
        printf("error in parsing: %s\n", res.val.err);
        err_msg = res.val.err;
    }
    file_parsed = true;
    return NULL;
}
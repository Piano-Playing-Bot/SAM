//////////////
// Includes //
//////////////

// @TODO: Send PIDI in stream cmds to the arduino
// @TODO: When first finding the Arduino Port, the loudness and speed levels should be set, as they might still be stored in modified form from a pevious session
// @TODO: Implement jumping on the timeline by sending new PIDI message with index 0

#define AIL_ALLOC_IMPL
#define AIL_ALL_IMPL
#define AIL_MD_IMPL
#define AIL_GUI_IMPL
#define AIL_FS_IMPL
#define AIL_BUF_IMPL
#define AIL_SV_IMPL

#include "../deps/raylib/src/raylib.h"  // For immediate UI framework
static RL_MouseCursor cursor;
#define SET_CURSOR(c) cursor = (c)
#define AIL_GUI_SET_CURSOR SET_CURSOR

#define ICON_SIZE_FACTOR 0.8f
#define MAX_SPEED 9.75f
#define MIN_SPEED 0.25f
#define MAX_VOLUME 2.0f
#define MIN_VOLUME 0.0f

#include "ail_fs.h"
#include "common.h"
#include <stdbool.h> // For boolean definitions
#include <string.h>  // For memcpy
#include <pthread.h> // For threads and mutexes
#include <unistd.h>  // For sleep @Cleanup
#include "math.h"    // For sinf, cosf
#include "midi.c"
#include "comm.c"
// #define AIL_ALLOC_PRINT_MEM
#include "ail_alloc.h"
#include "ail.h"
// #define AIL_MD_MEM_DEBUG
// #define AIL_MD_MEM_PRINT
#include "ail_md.h"
#include "ail_gui.h"
#include "ail_buf.h"
#include "ail_sv.h"

#define PRIMARY_COLOR          (RL_Color) { 0xff, 0x8c, 0x00, 0xff }
#define PRIMARY_BORDER_COLOR   (RL_Color) { 0x33, 0x33, 0x33, 0xff }
#define SECONDARY_COLOR        (RL_Color) { 0x80, 0x80, 0x80, 0xff }
#define SECONDARY_BORDER_COLOR (RL_Color) { 0x33, 0x33, 0x33, 0xff }
#define BG_COLOR               (RL_Color) { 0x00, 0x00, 0x00, 0xff }
#define SURFACE_COLOR          (RL_Color) { 0xff, 0x8c, 0x00, 0xff }
#define SURFACE_BORDER         (RL_Color) { 0xff, 0x8c, 0x00, 0xff }
#define ERROR_COLOR            (RL_Color) { 0xff, 0x00, 0x00, 0xff }
#define SUCC_COLOR             (RL_Color) { 0x00, 0xff, 0x00, 0xff }
#define ON_PRIMARY_COLOR       (RL_Color) { 0xff, 0xff, 0xff, 0xff }
#define ON_SECONDARY_COLOR     (RL_Color) { 0xff, 0xff, 0xff, 0xff }
#define ON_BG_COLOR            (RL_Color) { 0xff, 0xff, 0xff, 0xff }
#define ON_SURFACE_COLOR       (RL_Color) { 0xff, 0xff, 0xff, 0xff }
#define ON_ERROR_COLOR         (RL_Color) { 0xff, 0x00, 0x00, 0xff }

const AIL_Str data_dir_path    = { .str = "./data/", .len = 7 };
const AIL_Str library_filepath = { .str = "./data/library.pdil", .len = 19 };

#define FPS 60

typedef enum {
    UI_VIEW_LIBRARY,      // Show the library (possibly with search results)
    UI_VIEW_DND,          // Drag-n-Drop for adding a file
    UI_VIEW_ADD,          // Adding a new song (potentially changing name) after drag-n-drop
    UI_VIEW_PARSING_SONG, // In the process of uploading a new song
} UI_View;

static inline bool draw_icon(RL_Texture icon, u8 texture_idx, f32 x, f32 y, f32 icon_size, bool *pressed);
RL_Texture get_texture(const char *filepath);
AIL_DA(Song) search_songs(const char *substr);
void draw_loading_anim(RL_Rectangle bounds, bool start_new);
bool is_songname_taken(const char *name);
void  load_pidi(Song *song);
bool  save_pidi(Song song);
bool  save_library();
void *load_library(void *arg);
void *parse_file(void *_filepath);
void *util_memadd(const void *a, u64 a_size, const void *b, u64 b_size);


// These variables are all accessed by main and parse_file (and the functions called by parse_file)
char *filename;
char *song_name;
Song song;
static bool file_parsed;
static char *err_msg;

// These variables are all accessed by main and load_library
// @TODO: Make library into a Trie to simplify search
AIL_DA(Song) library = { .allocator = &ail_default_allocator };
bool library_ready = false;


UI_View view = UI_VIEW_LIBRARY;

int main(void)
{
    // Initialize memory arena for UI
    ail_gui_allocator = ail_alloc_arena_new(2*1024, &ail_alloc_std);
    u8 search_text_buffer[1024] = {0};
    AIL_Allocator search_text_allocator = ail_alloc_buffer_new(1024, search_text_buffer);

    u8   library_updated  = 0;
    bool is_music_playing = false;
    f64 cur_music_time = 0; // in ms
    f64 cur_music_len  = 0; // in ms

    i32 win_width  = 1200;
    i32 win_height = 600;
    RL_SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    RL_InitWindow(win_width, win_height, "Piano Player");
    RL_SetTargetFPS(FPS);

    char *file_path = NULL;
    pthread_t fileParsingThread;
    pthread_t loadLibraryThread;
    pthread_t commThread;

    pthread_create(&loadLibraryThread, NULL, load_library, NULL);
    pthread_create(&commThread, NULL, comm_thread_main, NULL);

    // Load Icons
#define ICON_TEXTURE_SIZE 512
    RL_Texture play_icon   = get_texture("assets/play.png");
    RL_Texture speed_icon  = get_texture("assets/speed.png");
    RL_Texture volume_icon = get_texture("assets/volume.png");
    RL_Texture back_icon   = get_texture("assets/back.png");
    bool paused = false;
    bool muted  = false;
    f32  speed  = 1.0f;
    f32  volume = 1.0f;
    set_volume(volume);
    set_speed(speed);

    static const f32 size_smaller = 35;
    static const f32 size_default = 50;
    static const f32 size_max     = size_default;
    RL_Font font = LoadFontEx("assets/Roboto-Regular.ttf", size_max, NULL, 95);
    AIL_Gui_Style style_default = {
        .color        = RL_WHITE,
        .bg           = RL_BLANK,
        .border_color = RL_BLANK,
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
    AIL_UNUSED(style_default_lt);
    AIL_Gui_Style style_button_default = {
        .color        = RL_WHITE,
        .bg           = (RL_Color){42, 230, 37, 255},
        .border_color = RL_BLANK,
        .border_width = 0,
        .font         = font,
        .font_size    = size_default,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = AIL_GUI_ALIGN_C,
        .vAlign       = AIL_GUI_ALIGN_C,
    };
    AIL_Gui_Style style_button_hover = ail_gui_cloneStyle(style_button_default);
    style_button_hover.border_color = (RL_Color){ 9, 170,  6, 255 };
    style_button_hover.border_width = 5;
    AIL_Gui_Style style_button_disabled_default = ail_gui_cloneStyle(style_button_default);
    style_button_disabled_default.bg = RL_GRAY;
    AIL_Gui_Style style_button_disabled_hover = ail_gui_cloneStyle(style_button_disabled_default);
    style_button_disabled_hover.border_color = (RL_Color){ 100, 100, 100, 255 };
    style_button_disabled_hover.border_width = 5;
    AIL_Gui_Style style_song_name_default = {
        .color        = RL_WHITE,
        .bg           = RL_LIGHTGRAY,
        .border_color = RL_GRAY,
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
    style_song_name_hover.bg = RL_GRAY;
    AIL_Gui_Style style_search = {
        .color        = RL_WHITE,
        .bg           = RL_BLANK,
        .border_color = RL_GRAY,
        .font         = font,
        .border_width = 5,
        .font_size    = size_smaller,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 15,
        .hAlign       = AIL_GUI_ALIGN_LT,
        .vAlign       = AIL_GUI_ALIGN_C,
    };
    AIL_Gui_Style style_warn_text = {
        .color        = RL_YELLOW,
        .bg           = RL_BLANK,
        .border_color = RL_BLANK,
        .font         = font,
        .border_width = 0,
        .font_size    = size_default,
        .cSpacing     = 2,
        .lSpacing     = 5,
        .pad          = 0,
        .hAlign       = AIL_GUI_ALIGN_C,
        .vAlign       = AIL_GUI_ALIGN_C,
    };

    RL_Rectangle header_bounds, content_bounds, play_bounds, icon_bounds;
    u32 play_timeline_height, play_timeline_width, icon_size, header_y_pad, header_x_pad, play_bounds_pad, play_inner_pad;
    f32 conn_circ_radius, play_circ_radius, volume_slider_width, volume_slider_height, volume_circ_radius, speed_max_size, icon_pad;
    AIL_Gui_Label centered_label = {
        .text         = ail_da_new_empty(char),
        .defaultStyle = style_default,
        .hovered      = style_default,
    };
    char *upload_btn_msg = "Upload";
    AIL_Gui_Label upload_button = {
        .text         = ail_da_from_parts(char, upload_btn_msg, strlen(upload_btn_msg), strlen(upload_btn_msg), &ail_default_allocator),
        .defaultStyle = style_button_default,
        .hovered      = style_button_hover,
    };

#define SET_VIEW(v) do { view = (v); view_changed = true; } while(0)
    bool is_first_frame    = true;
    bool view_changed      = false;
    bool view_prev_changed = false;
    while (!RL_WindowShouldClose()) {
        RL_BeginDrawing();

        bool is_resized = RL_IsWindowResized() || is_first_frame;
        is_first_frame = false;
        if (is_resized) {
            win_width    = RL_GetScreenWidth();
            win_height   = RL_GetScreenHeight();
            header_y_pad = AIL_CLAMP(win_height / 80, 0, 15);
            header_x_pad = AIL_CLAMP(win_width/50, 0, 100);
            // Bounds of Header at top
            // Header Layout (horizontal perspective):
            // --------------------
            // <Connection Status Circle>
            // <Search Bar>
            // <Upload Button>
            // --------------------
            header_bounds  = (RL_Rectangle) {
                .x = header_x_pad,
                .y = 0,
                .width  = win_width - 2*header_x_pad,
                .height = AIL_CLAMP(win_height / 6, size_max + 30, size_max*2)
            };

            // Bounds of Footer
            // Footer Layout (vertical perspective):
            // --------------------
            //       <play_bounds_pad>
            //  <Speed Icons> <Volume Icons>
            //       <play_inner_pad>
            //   <pause/continue> <timeline>
            //       <play_bounds_pad>
            // --------------------
            play_bounds_pad = header_x_pad;
            play_inner_pad  = AIL_CLAMP(play_bounds_pad, 5, 15);
            icon_size = size_max*3/2;
            icon_pad  = play_inner_pad;
            play_timeline_height = 10;
            play_timeline_width  = play_bounds.width - icon_size - icon_pad;
            play_circ_radius     = AIL_CLAMP(play_bounds_pad, 1.3f*play_timeline_height, 2*play_timeline_height);
            i32 play_bounds_min_height = AIL_MAX(2*size_default, play_inner_pad + play_timeline_height + icon_size + 2*play_bounds_pad);
            play_bounds.width  = header_bounds.width;
            play_bounds.height = play_bounds_min_height; //AIL_CLAMP(win_height/10, play_bounds_min_height, win_height/4);
            play_bounds.x      = play_bounds_pad;
            play_bounds.y      = win_height - play_bounds.height;

            icon_bounds = (RL_Rectangle) {
                .x = play_bounds.x,
                .y = play_bounds.y + play_bounds_pad,
                .width  = play_bounds.width,
                .height = icon_size,
            };
            u8 icons_count = 4;
            speed_max_size = 4*size_smaller; // Just an estimate based on expecting no more than 4 characters at most (e.g. 9.75x) in size_smaller font-size
            f32 volume_slider_x  = speed_max_size + icon_pad + icons_count*(icon_size + icon_pad);
            volume_slider_width  = AIL_MIN(AIL_MAX(4*size_default, (icon_bounds.width - volume_slider_x) / 3), icon_bounds.width - volume_slider_x);
            volume_slider_height = AIL_MAX(5, play_timeline_height - 5);
            volume_circ_radius   = AIL_CLAMP(icon_pad, 1.3f*volume_slider_height, 2.0f*volume_slider_height);

            // Bounds of Song Library
            content_bounds.x = header_bounds.x;
            content_bounds.y = header_bounds.y + header_bounds.height;
            content_bounds.width  = header_bounds.width;
            content_bounds.height = win_height - content_bounds.y - play_bounds.height;
        }
        RL_ClearBackground(BG_COLOR);
        SET_CURSOR(MOUSE_CURSOR_DEFAULT);

        if (view_prev_changed && view_changed) view_changed = false;
        else if (view_changed) { view_prev_changed = true; view_changed = false; }
        else if (view_prev_changed) view_prev_changed = false;

        bool requires_recalc = is_resized || view_changed || view_prev_changed;

        // if (library_updated > 0) library_updated--;
        library_updated -= (library_updated > 0);


        static f32 scroll = 0;
        if (view_changed) scroll = 0.0f;
        f32 scroll_delta = -10 * GetMouseWheelMove();
        scroll += scroll_delta;
        if (AIL_UNLIKELY(scroll < 0.0f)) scroll = 0.0f;

        if (requires_recalc) centered_label.bounds = (RL_Rectangle){0, 0, win_width, win_height};

        switch(view) {
            // @TODO: Display song timeline at the bottom (allowing user to jump back and forth on it)
            case UI_VIEW_LIBRARY: {
                // Recalculate cached sizes if necessary
                if (requires_recalc) {
                    conn_circ_radius = header_bounds.height/2 - header_y_pad;
                    RL_Vector2 upload_txt_size = MeasureTextEx(upload_button.hovered.font, upload_button.text.data, upload_button.hovered.font_size, upload_button.hovered.cSpacing);
                    upload_button.bounds.y      = header_bounds.y + header_y_pad + upload_button.hovered.border_width;
                    upload_button.bounds.height = header_bounds.height - upload_button.bounds.y - upload_button.hovered.border_width - header_y_pad;
                    upload_button.bounds.width  = upload_button.hovered.border_width*2 + upload_button.hovered.pad*2 + upload_txt_size.x;
                    upload_button.bounds.x      = header_bounds.x + header_bounds.width - header_y_pad - upload_button.bounds.width;
                }


                // Show loading animation if library is being loaded
                static bool loaded_lib_in_prev_frame = false;
                if (AIL_UNLIKELY(!library_ready)) {
                    loaded_lib_in_prev_frame = true;
                    char *loading_lib_text = "Loading Libary...";
                    centered_label.text    = ail_da_from_parts(char, loading_lib_text, strlen(loading_lib_text), strlen(loading_lib_text), &ail_default_allocator);
                    ail_gui_drawLabel(centered_label);
                    SET_CURSOR(MOUSE_CURSOR_DEFAULT);
                }
                // Show library otherwise
                else {
                    RL_Color conn_color = comm_is_connected ? RL_GREEN : RL_BLANK;
                    DrawCircle     (header_bounds.x + conn_circ_radius, header_bounds.y + conn_circ_radius + header_y_pad, conn_circ_radius, conn_color);
                    DrawCircleLines(header_bounds.x + conn_circ_radius, header_bounds.y + conn_circ_radius + header_y_pad, conn_circ_radius, RL_WHITE);

                    AIL_Gui_State upload_button_state = ail_gui_drawLabel(upload_button);
                    if (upload_button_state == AIL_GUI_STATE_PRESSED) SET_VIEW(UI_VIEW_DND);

                    static char *search_text = "";
                    static char *search_placeholder = "Search...";
                    static RL_Rectangle       search_bounds;
                    static AIL_Gui_Input_Box  search_input_box;
                    static AIL_Gui_Update_Res search_res;

                    if (AIL_UNLIKELY(requires_recalc || loaded_lib_in_prev_frame)) {
                        u32 search_x = header_bounds.x + 2*conn_circ_radius + header_x_pad;
                        search_bounds = (RL_Rectangle) {
                            .x = search_x,
                            .y = header_bounds.y + style_search.border_width + header_y_pad,
                            .width  = upload_button.bounds.x - upload_button.hovered.border_width - header_x_pad - style_search.border_width - search_x,
                            .height = header_bounds.height - 2*style_search.border_width - 2*header_y_pad,
                        };
                        AIL_Gui_Label search_label = {
                            .bounds       = search_bounds,
                            .text         = ail_da_from_parts(char, search_text, 1, 1, &search_text_allocator),
                            .defaultStyle = style_search,
                            .hovered      = style_search,
                        };
                        search_input_box = ail_gui_newInputBox(search_placeholder, false, false, true, search_label);
                    }
                    search_res  = ail_gui_drawInputBox(&search_input_box);
                    search_text = search_input_box.label.text.data;

                    static AIL_DA(Song) songs;
                    if (search_res.updated && search_input_box.label.text.len) {
                        if (songs.data != library.data && songs.data) ail_da_free(&songs);
                        songs = search_songs(search_input_box.label.text.data);
                    } else if (!songs.data || library_updated) songs = library;


                    // @Cleanup: Magic numbers hidden deep inside function
                    static const u32 song_name_width  = 200;
                    static const u32 song_name_height = 150;
                    static const u32 song_name_margin = 50;
                    u32 full_song_name_width  = song_name_width  + 2*style_song_name_default.pad + 2*style_song_name_default.border_width;
                    u32 full_song_name_height = song_name_height + 2*style_song_name_default.pad + 2*style_song_name_default.border_width;
                    u32 song_names_per_row    = (content_bounds.width + song_name_margin) / (song_name_margin + full_song_name_width);
                    u32 rows_amount           = (songs.len / song_names_per_row) + ((songs.len % song_names_per_row) > 0);
                    u32 full_width            = song_names_per_row*full_song_name_width + (song_names_per_row - 1)*song_name_margin;
                    u32 start_x               = (content_bounds.width - full_width) / 2;
                    u32 virtual_height        = rows_amount*(full_song_name_height + song_name_margin);
                    f32 max_y                 = (virtual_height > content_bounds.height) ? virtual_height - content_bounds.height : 0;
                    scroll = AIL_MIN(scroll, max_y);
                    u32 start_row             = scroll / (full_song_name_height + song_name_margin);

                    for (u32 i = start_row * song_names_per_row; i < songs.len; i++) {
                        RL_Rectangle song_bounds = {
                            start_x + (full_song_name_width + song_name_margin)*(i % song_names_per_row) + 2*style_song_name_default.border_width,
                            content_bounds.y + song_name_margin + (full_song_name_height + song_name_margin)*(i / song_names_per_row) - scroll,
                            song_name_width,
                            song_name_height
                        };
                        char *song_name = songs.data[i].name;
                        AIL_Gui_Label song_label = {
                            .text         = ail_da_from_parts(char, song_name, strlen(song_name), strlen(song_name), &ail_default_allocator),
                            .bounds       = song_bounds,
                            .defaultStyle = style_song_name_default,
                            .hovered      = style_song_name_hover,
                        };
                        AIL_Gui_State song_label_state = ail_gui_drawLabelOuterBounds(song_label, content_bounds);
                        if (song_label_state == AIL_GUI_STATE_PRESSED && comm_is_connected) {
                            DBG_LOG("Playing song: %s\n", song_name);
                            // @TODO: Display hover style of songs differently if not connected maybe?
                            // @TODO: Reading file blocks UI thread...
                            Song s = songs.data[i];
                            load_pidi(&s);
                            printf("\033[33mSending song with %d commands\033[0m\n", s.cmds.len);
                            send_new_song(s.cmds, 0);
                            is_music_playing = true;
                            cur_music_len    = s.len;
                            cur_music_time   = 0;
                        }
                    }
                }


                // If music is playing -> show timeline & music controls
                if (is_music_playing) {
                    if (!comm_is_music_playing) {
                        draw_loading_anim(play_bounds, false);
                    } else {
                        static bool timeline_selected = false;
                        f32 played_perc = AIL_MIN(cur_music_time / cur_music_len, 1.0f);
                        RL_Rectangle total_rect = {
                            .x      = play_bounds.x,
                            .y      = play_bounds.y + play_bounds.height - play_timeline_height - play_bounds_pad,
                            .width  = play_bounds.width,
                            .height = play_timeline_height,
                        };

                        RL_Vector2 mouse = GetMousePosition();
                        bool timeline_hovered = ail_gui_isPointInRec(mouse.x, mouse.y, total_rect.x, total_rect.y - 2*total_rect.height, total_rect.width, 4*total_rect.height);

                        // Draw Icons
                        bool any_icon_hovered = false;
                        #define FLOAT_TEXT_LEN 8
                        static char speed_text[FLOAT_TEXT_LEN] = {0};
                        { // snprintf(speed_text, FLOAT_TEXT_LEN, "%.2fx", speed) except no decimal digits are printed if they are 0
                            u8 d1 = ((u8)(speed*10 ))%10;
                            u8 d2 = ((u8)(speed*100))%10;
                            if (!d1 && !d2) snprintf(speed_text, FLOAT_TEXT_LEN, "%.0fx", speed);
                            else if (!d2)   snprintf(speed_text, FLOAT_TEXT_LEN, "%.1fx", speed);
                            else            snprintf(speed_text, FLOAT_TEXT_LEN, "%.2fx", speed);
                        }
                        RL_Vector2 speed_text_size = MeasureTextEx(font, speed_text, size_smaller, 0);
                        RL_Vector2 speed_text_pos  = {
                            .x = icon_bounds.x + speed_max_size - speed_text_size.x,
                            .y = icon_bounds.y + (icon_bounds.height - speed_text_size.y)/2,
                        };
                        RL_DrawTextEx(font, speed_text, speed_text_pos, size_smaller, 0, RL_WHITE);
                        f32 icon_x = icon_bounds.x + speed_max_size + icon_pad;
                        f32 icon_y = icon_bounds.y;
                        bool pressed;
                        any_icon_hovered |= draw_icon(speed_icon, 0, icon_x, icon_y, icon_size, &pressed);
                        if (pressed) {
                            speed = AIL_MAX(speed - 0.25f, MIN_SPEED);
                            set_speed(speed);
                        }
                        icon_x += icon_size + icon_pad;
                        any_icon_hovered |= draw_icon(play_icon, !paused, icon_x, icon_y, icon_size, &pressed);
                        if (pressed) {
                            paused = !paused;
                            set_paused(paused);
                        }
                        icon_x += icon_size + icon_pad;
                        any_icon_hovered |= draw_icon(speed_icon, 1, icon_x, icon_y, icon_size, &pressed);
                        if (pressed) {
                            speed = AIL_MIN(speed + 0.25f, MAX_SPEED);
                            set_speed(speed);
                        }
                        icon_x += icon_size + icon_pad;
                        static bool volume_hovered = false;
                        bool volume_icon_hovered = draw_icon(volume_icon, (muted || volume < 0.1f) ? 0 : 1 + (volume >= 0.5f), icon_x, icon_y, icon_size, &pressed);
                        icon_x += icon_size + icon_pad;
                        if (pressed) {
                            muted = !muted;
                            if (muted) set_volume(0.0f);
                            else set_volume(volume);
                        }
                        if (volume_hovered || volume_icon_hovered) {
                            RL_Rectangle volume_slider = {
                                .x = icon_x,
                                .y = icon_y + (icon_size - volume_slider_height)/2,
                                .width  = volume_slider_width,
                                .height = volume_slider_height,
                            };
                            RL_Rectangle volume_slider_filled = volume_slider;
                            volume_slider_filled.y      -= 2.0f;
                            volume_slider_filled.height += 4.0f;

                            // @TODO: Show volume in percentage on screen?
                            volume_hovered = ail_gui_isPointInRec(mouse.x, mouse.y, icon_x - icon_pad, icon_y, volume_slider.width + 4*icon_pad, icon_size);
                            if (ail_gui_isPointInRec(mouse.x, mouse.y, icon_x, volume_slider.y - 2*volume_slider.height, volume_slider.width, 4*volume_slider.height)) {
                                SET_CURSOR(MOUSE_CURSOR_POINTING_HAND);
                                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                                    volume = AIL_LERP(AIL_CLAMP(AIL_REV_LERP(mouse.x, volume_slider.x, volume_slider.x + volume_slider.width), 0.0f, 1.0f), MIN_VOLUME, MAX_VOLUME);
                                    muted  = false;
                                    set_volume(volume);
                                }
                            }
                            volume_hovered |= volume_icon_hovered;

                            DrawRectangleRounded(volume_slider, 5.0f, 5, RL_GRAY);
                            DrawRectangleRounded(volume_slider_filled, 5.0f, 5, RL_RED);
                            DrawCircle(volume_slider.x + volume/MAX_VOLUME*volume_slider.width, volume_slider.y + volume_slider.height/2, volume_circ_radius, RL_RED);
                        }
                        any_icon_hovered |= volume_hovered;

                        if (any_icon_hovered) {
                            timeline_selected = false;
                            timeline_hovered  = false;
                        }

                        f32 x_circle;
                        if (timeline_selected) {
                            SET_CURSOR(MOUSE_CURSOR_POINTING_HAND);
                            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                                x_circle = mouse.x;
                            } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                                goto timeline_jump;
                            }
                        } else if (timeline_hovered) {
                            SET_CURSOR(MOUSE_CURSOR_POINTING_HAND);
                            timeline_selected = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
                            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
timeline_jump:
                                played_perc    = ((f32)(mouse.x - total_rect.x))/(f32)total_rect.width;
                                cur_music_time = AIL_LERP(played_perc, 0, cur_music_len);
                                send_new_song(comm_cmds, (u32)cur_music_time);
                                timeline_selected = false;
                            }
                        }
                        if (!timeline_selected) {
                            x_circle = AIL_LERP(played_perc, play_bounds.x, play_bounds.x + play_bounds.width);
                            timeline_selected = false;
                        }

                        RL_Rectangle played_rect = total_rect;
                        played_rect.width   = x_circle - play_bounds.x;
                        played_rect.y      -= 2.0f;
                        played_rect.height += 4.0f;

                        DrawRectangleRounded(total_rect,  5.0f, 5, RL_GRAY);
                        DrawRectangleRounded(played_rect, 5.0f, 5, RL_RED);
                        DrawCircle(x_circle, played_rect.y + played_rect.height/2, play_circ_radius, RL_RED);

                        if (!paused) cur_music_time += speed * RL_GetFrameTime() * 1000.0f;
                    }
                }
                // Otherwise if Arduino is not connected, show text reminding user to connect
                else if (!comm_is_connected) {
                    static const char *not_connected_msg = "Please connect to the Arduino to play any music...";
                    ail_gui_drawText(not_connected_msg, play_bounds, style_warn_text);
                }
            } break;

            case UI_VIEW_DND: {
                static char *dnd_view_msg;
                if (view_changed || view_prev_changed) {
                    RL_UnloadDroppedFiles(RL_LoadDroppedFiles());
                    dnd_view_msg = "Drag-and-Drop a MIDI-File to play it on the Piano";
                }

                bool pressed = false;
                draw_icon(back_icon, 0, header_bounds.x, header_bounds.y, icon_size, &pressed);
                if (pressed) {
                    SET_VIEW(UI_VIEW_LIBRARY);
                }

                centered_label.text = ail_da_from_parts(char, dnd_view_msg, strlen(dnd_view_msg), strlen(dnd_view_msg), &ail_default_allocator);
                ail_gui_drawLabel(centered_label);

                if (RL_IsFileDropped()) {
                    RL_FilePathList dropped_files = RL_LoadDroppedFiles();
                    AIL_SV fpath = ail_sv_from_cstr(dropped_files.paths[0]);
                    if (dropped_files.count > 1 || !RL_IsPathFile(dropped_files.paths[0])) {
                        // @TODO: Allow a list of midi files
                        dnd_view_msg = "You can only drag and drop a single MIDI-File to play it on the Piano.\nPlease try again";
                    } else if (!ail_sv_ends_with(fpath, ail_sv_from_cstr(".mid")) && !ail_sv_ends_with(fpath, ail_sv_from_cstr(".mid"))) {
                        dnd_view_msg = "You can only drag and drop MIDI-Files to play it on the Piano.\nMake sure the file has the extension '.mid' or '.midi'.\nPlease try again.";
                    } else {
                        SET_VIEW(UI_VIEW_ADD);
                        u64 path_len = strlen(dropped_files.paths[0]);
                        file_path   = malloc(sizeof(char) * (path_len + 1));
                        memcpy(file_path, dropped_files.paths[0], path_len + 1);
                        pthread_create(&fileParsingThread, NULL, parse_file, (void *)file_path);
                    }
                    RL_UnloadDroppedFiles(dropped_files);
                }
            } break;

            case UI_VIEW_ADD: {
                static bool              btn_selected = false;
                static RL_Rectangle      input_bounds = { 0 };
                static AIL_Gui_Label     name_label   = { 0 };
                static AIL_Gui_Input_Box name_input   = { 0 };
                if (requires_recalc) {
                    u32 input_margin = AIL_MAX(5, win_width - AIL_CLAMP(win_width*8/10, 200, 1000));
                    input_bounds = (RL_Rectangle) { input_margin, (win_height - style_default.font_size) / 2, win_width - 2*input_margin, style_default.font_size + 2*style_default.pad };
                    name_label   = ail_gui_newLabel(input_bounds, name_label.text.data ? name_label.text.data : filename, style_default, style_default);
                    name_input   = ail_gui_newInputBox("Name of RL_Music", false, false, true, name_label);
                }

                bool pressed = false;
                draw_icon(back_icon, 0, header_bounds.x, header_bounds.y, icon_size, &pressed);
                if (pressed) {
                    // @Memory: Potentially leaking `song_name` here...
                    SET_VIEW(UI_VIEW_LIBRARY);
                }

                bool valid_name = name_input.label.text.len > 0 && !is_songname_taken(name_input.label.text.data);
                AIL_Gui_Style input_style = ail_gui_cloneStyle(style_default);
                input_style.border_width      = 5;
                input_style.border_color      = valid_name ? RL_GREEN : RL_RED;
                input_style.bg                = BG_COLOR;
                name_input.label              = name_label;
                name_input.label.defaultStyle = input_style;
                name_input.label.hovered      = input_style;
                name_input.selected           = !btn_selected;
                AIL_Gui_Update_Res res        = ail_gui_drawInputBox(&name_input);

                u32 btn_text_size     = MeasureTextEx(style_button_default.font, upload_btn_msg, style_button_default.font_size, style_button_default.cSpacing).x;
                i32 btn_width         = btn_text_size + 2*style_button_default.border_width + 2*style_button_default.pad;
                RL_Rectangle button_bounds = {
                    input_bounds.x + input_bounds.width - btn_width,
                    input_bounds.y + input_bounds.height + 15,
                    btn_width,
                    style_button_default.font_size + 2*style_button_default.pad
                };
                AIL_Gui_Label add_button = ail_gui_newLabel(button_bounds, upload_btn_msg, valid_name ? style_button_default : style_button_disabled_default, valid_name ? style_button_hover : style_button_disabled_hover);
                AIL_Gui_State btn_res    = ail_gui_drawLabel(add_button);
                ail_gui_freeLabel(&add_button);

                if (res.tab || IsKeyPressed(KEY_TAB))   btn_selected = !btn_selected;
                if (res.state >= AIL_GUI_STATE_PRESSED) btn_selected = false;
                if (valid_name && (res.enter || btn_res >= AIL_GUI_STATE_PRESSED)) {
                    song_name = name_input.label.text.data;
                    DBG_LOG("song_name: %s\n", song_name);
                    // @Memory: Potentially leaking `song_name` here???
                    SET_VIEW(UI_VIEW_PARSING_SONG);
                }
            } break;

            case UI_VIEW_PARSING_SONG: {
                draw_loading_anim((RL_Rectangle){0, 0, win_width, win_height}, view_changed);
                if (file_parsed) {
                    song.name = song_name;
                    ail_da_push(&library, song);
                    library_updated = 2; // setting it to 2 instead of true, because we reduce it by 1 each frame (up to 0) and thus it will still be greater 0 when being checked next frame
                    if (!save_pidi(song)) AIL_TODO();
                    if (!save_library()) AIL_TODO();
                    SET_VIEW(UI_VIEW_LIBRARY);
                }
            } break;
        }

        RL_DrawFPS(10, 10); // @Cleanup

        SetMouseCursor(cursor);
        RL_EndDrawing();
        ail_gui_allocator.free_all(ail_gui_allocator.data);
    }

    if (comm_port) CloseHandle(comm_port);
    RL_CloseWindow();
    return 0;
}

void draw_loading_anim(RL_Rectangle bounds, bool start_new)
{
    static u32 loading_anim_idx             = 0;
    const  f32 loading_anim_circle_radius   = AIL_CLAMP(AIL_MIN(bounds.width, bounds.height)/30, 5, 30);
    const  u8  loading_anim_circle_count    = AIL_CLAMP(AIL_MIN(bounds.width, bounds.height)/20, 6, 16);
    const  f32 loading_anim_circle_distance = AIL_CLAMP(AIL_MIN(bounds.width, bounds.height)/6,  5, 200);
    const  u32 loading_anim_len             = 7*loading_anim_circle_count;
    if (AIL_UNLIKELY(start_new)) loading_anim_idx = 0;
    u8 loading_cur_song = (u8)(((f32) loading_anim_circle_count * loading_anim_idx) / (f32)loading_anim_len);
    for (u8 i = 0; i < loading_anim_circle_count; i++) {
        f32 i_perc = i / (f32)loading_anim_circle_count;
        u32 x      = bounds.x + bounds.width/2  + loading_anim_circle_distance*cosf(2*PI*i_perc);
        u32 y      = bounds.y + bounds.height/2 + loading_anim_circle_distance*sinf(2*PI*i_perc);
        f32 delta  = (i + loading_anim_circle_count - loading_cur_song) / (f32)loading_anim_circle_count;
        RL_Color col = {0xff, 0xff, 0xff, 0xff*delta};
        DrawCircle(x, y, loading_anim_circle_radius, col);
    }
    loading_anim_idx = (loading_anim_idx + 1) % loading_anim_len;
}


RL_Texture get_texture(const char *filepath)
{
    RL_Image img = RL_LoadImage(filepath);
    AIL_ASSERT(img.data);
    RL_Texture texture = LoadTextureFromImage(img);
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    AIL_ASSERT(texture.id);
    return texture;
}

bool draw_icon(RL_Texture icon, u8 texture_idx, f32 x, f32 y, f32 icon_size, bool *pressed)
{
    RL_Vector2 mouse = GetMousePosition();
    *pressed = false;
    bool hovered = ail_gui_isPointInRec(mouse.x, mouse.y, x, y, icon_size, icon_size);
    if (hovered) {
        DrawCircle(x + icon_size/2, y + icon_size/2, icon_size/2, (RL_Color) { 0xff, 0xff, 0xff, 0x33 });
        SET_CURSOR(MOUSE_CURSOR_POINTING_HAND);
        *pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }
    RL_Color col = ColorBrightness(RL_WHITE, hovered ? 1.0f : -0.1f);
    DrawCircleLines(x + icon_size/2, y + icon_size/2, icon_size/2, col);
    RL_Rectangle src = {
        .x = ICON_TEXTURE_SIZE*texture_idx,
        .y = 0,
        .width  = ICON_TEXTURE_SIZE,
        .height = ICON_TEXTURE_SIZE
    };
    RL_Rectangle dst = {
        .x = x + (icon_size - ICON_SIZE_FACTOR*icon_size)/2,
        .y = y + (icon_size - ICON_SIZE_FACTOR*icon_size)/2,
        .width  = ICON_SIZE_FACTOR*icon_size,
        .height = ICON_SIZE_FACTOR*icon_size,
    };
    DrawTexturePro(icon, src, dst, (RL_Vector2){ 0 }, 0, col);
    return hovered;
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

bool is_songname_taken(const char *name)
{
    for (u32 i = 0; i < library.len; i++) {
        if (strcmp(name, library.data[i].name) == 0) return true;
    }
    return false;
}

bool is_prefix(const char *restrict prefix, const char *restrict str, bool ignore_case)
{
    bool is_prefix = true;
    u32 i = 0;
    for (; str[i] && prefix[i] && is_prefix; i++) {
        char c1 = str[i];
        char c2 = prefix[i];
        if (ignore_case && str[i]    >= 'A' && str[i]    <= 'Z') c1 += 'a' - 'A';
        if (ignore_case && prefix[i] >= 'A' && prefix[i] <= 'Z') c2 += 'a' - 'A';
        is_prefix = c1 == c2;
    }
    return is_prefix && !prefix[i];
}

AIL_DA(Song) search_songs(const char *substr)
{
    AIL_DA(Song) prefixed    = ail_da_new(Song);
    AIL_DA(Song) substringed = ail_da_new(Song);
    u32 substr_len = strlen(substr);
    // DBG_LOG("substr: %s\n", substr);
    // DBG_LOG("library: { data: %p, len: %d, cap: %d }\n", (void *)library.data, library.len, library.cap);
    for (u32 i = 0; i < library.len; i++) {
        // Check for prefix
        // DBG_LOG("i: %d, s: %s\n", i, library.data[i].name);
        if (is_prefix(substr, library.data[i].name, true)) {
            // DBG_LOG("is prefix\n");
            ail_da_push(&prefixed, library.data[i]);
        } else {
            // Check for substring
            bool is_substr = false;
            u32 name_len = strlen(library.data[i].name);
            if (name_len > substr_len) {
                for (u32 j = 1; !is_substr && j <= name_len - substr_len; j++) {
                    // DBG_LOG("j: %d\n", j);
                    is_substr = is_prefix(substr, &library.data[i].name[j], true);
                }
            }
            // if (is_substr) DBG_LOG("is substring\n");
            if (is_substr) ail_da_push(&substringed, library.data[i]);
        }
    }
    ail_da_pushn(&prefixed, substringed.data, substringed.len);
    ail_da_free(&substringed);
    return prefixed;
}

// loads the PIDI-file (as referred to by song->name) into song
void load_pidi(Song *song)
{
    u64 data_dir_path_len = data_dir_path.len;
    u64 name_len          = strlen(song->name);
    char *fname = malloc(data_dir_path_len + name_len + 6);
    memcpy(fname, data_dir_path.str, data_dir_path_len);
    memcpy(&fname[data_dir_path_len], song->name, name_len);
    memcpy(&fname[data_dir_path_len + name_len], ".pidi", 6);
    AIL_Buffer buf = ail_buf_from_file(fname);
    free(fname);
    AIL_ASSERT(ail_buf_read4msb(&buf) == PIDI_MAGIC);
    u32 n = ail_buf_read4lsb(&buf);
    song->cmds.data = song->cmds.allocator->alloc(song->cmds.allocator->data, n * sizeof(PidiCmd));
    song->cmds.cap  = n;
    song->cmds.len  = n;
    for (u32 i = 0; i < n; i++) {
        song->cmds.data[i] = decode_cmd(&buf);
    }
}

bool save_pidi(Song song)
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write4msb(&buf, PIDI_MAGIC);
    ail_buf_write4lsb(&buf, song.cmds.len);
    for (u32 i = 0; i < song.cmds.len; i++) {
        encode_cmd(&buf, song.cmds.data[i]);
    }

    u64 data_dir_path_len = data_dir_path.len;
    u64 name_len          = strlen(song.name);
    char *fname = malloc(data_dir_path_len + name_len + 6);
    memcpy(fname, data_dir_path.str, data_dir_path_len);
    memcpy(&fname[data_dir_path_len], song.name, name_len);
    memcpy(&fname[data_dir_path_len + name_len], ".pidi", 6);
    bool out = ail_buf_to_file(&buf, fname);
    free(fname);
    return out;
}

bool save_library()
{
    AIL_Buffer buf = ail_buf_new(1024);
    ail_buf_write4msb(&buf, PDIL_MAGIC);
    ail_buf_write4lsb(&buf, library.len);
    for (u32 i = 0; i < library.len; i++) {
        Song song = library.data[i];
        u32 name_len  = strlen(song.name);
        ail_buf_write4lsb(&buf, name_len);
        ail_buf_write8lsb(&buf, song.len);
        ail_buf_writestr(&buf, song.name, name_len);
    }
    if (!RL_DirectoryExists(data_dir_path.str)) mkdir(data_dir_path.str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    return ail_buf_to_file(&buf, library_filepath.str);
}

void *load_library(void *arg)
{
    (void)arg;
    library_ready = false;
    ail_da_free(&library);

    if (!RL_DirectoryExists(data_dir_path.str)) {
        mkdir(data_dir_path.str, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        goto nothing_to_load;
    } else {
        if (!RL_FileExists(library_filepath.str)) goto nothing_to_load;
        AIL_Buffer buf = ail_buf_from_file(library_filepath.str);
        if (ail_buf_read4msb(&buf) != PDIL_MAGIC) goto nothing_to_load;
        u32 n = ail_buf_read4lsb(&buf);
        ail_da_maybe_grow(&library, n);
        for (; n > 0; n--) {
            u32 name_len = ail_buf_read4lsb(&buf);
            u64 song_len = ail_buf_read8lsb(&buf);
            char *name   = ail_buf_readstr(&buf, name_len);
            Song song = {
                .name   = name,
                .len    = song_len,
                .cmds = ail_da_new_empty(PidiCmd),
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

void *parse_file(void *_filepath)
{
    file_parsed    = false;
    char *filepath = (char *)_filepath;
    i32 path_len   = strlen(filepath);
    filename       = filepath;
    i32 name_len   = path_len;
    for (i32 i = path_len-2; i > 0; i--) {
        if (filepath[i] == '/' || (filepath[i] == '\\' && filepath[i+1] != ' ')) {
            filename = &filepath[i+1];
            name_len  = path_len - 1 - i;
            break;
        }
    }

    #define MIDI_EXT_LEN 4
    if (path_len < MIDI_EXT_LEN || memcmp(&filepath[path_len - MIDI_EXT_LEN], ".mid", MIDI_EXT_LEN) != 0) {
        sprintf(err_msg, "%s is not a midi file\n", filename);
        return NULL;
    }

    // Remove file-ending from filename
    name_len -= MIDI_EXT_LEN;
    char *new_filename = malloc((name_len + 1) * sizeof(char));
    memcpy(new_filename, filename, name_len);
    new_filename[name_len] = 0;
    filename = new_filename;

    AIL_Buffer buffer = ail_buf_from_file(filepath);

    ParseMidiRes res = parse_midi(buffer);
    err_msg = NULL;
    if (res.succ) {
        res.val.song.name = filename;
        song = res.val.song;
    }
    else {
        DBG_LOG("error in parsing: %s\n", res.val.err);
        err_msg = res.val.err;
    }
    file_parsed = true;
    return NULL;
}
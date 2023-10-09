#ifndef GUI_H_
#define GUI_H_

#include "util.h"
#include "raylib.h"

typedef enum __attribute__((__packed__)) {
    EL_STATE_HIDDEN,   // Element is not displayed
    EL_STATE_INACTIVE, // Element is not active in any way. `state > EL_STATE_INACTIVE` can be used to check if the element is active in anyway
    EL_STATE_HOVERED,  // Element is currently being hovered
    EL_STATE_PRESSED,  // Element is just being clicked on in this frame. It is active from now on
    EL_STATE_FOCUSED,  // Element is being focused on and active. To be active is to accept user input
} Gui_El_State;

typedef struct {
    Color bg;           // Color for background
    Color border_color; // Color for border
    i32 border_width;   // Width of border
    // The following is only relevant for text
    Color color;        // Color for text
    i32 pad;            // Padding; Space between text and border
    Font font;
    float font_size;    // Font Size
    float spacing;      // Spacing between characters of text
} Gui_El_Style;

typedef struct {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    char *text;
    Gui_El_Style defaultStyle;
    Gui_El_Style hovered;
} Gui_Label;

typedef struct {
    char *placeholder;      // Placeholder
    Gui_Label label;        // Gui_Label to display and update on input
    u32 cur;                // Index in the text at which the cursor should be displayed
    i32 anim_idx;           // Current index in playing animation - if negative, it is currently in waiting time
    u16 rows;               // Amount of rows in label.text. If there's no newline, `rows == 1`
    bool resize;
    bool multiline;
    bool selected;
    // @TODO:
} Gui_Input_Box;

typedef struct {
    bool updated;
    bool enter;
    bool tab;
    Gui_El_State state;
} Gui_Update_Res;

void gui_setTextLineSpacing(i32 spacing);
bool gui_stateIsActive(Gui_El_State state);
bool gui_isPointInRec(i32 px, i32 py, i32 rx, i32 ry, i32 rw, i32 rh);
Gui_El_Style gui_defaultStyle(Font font);
Gui_El_Style gui_cloneStyle(Gui_El_Style self);
void gui_drawSized(Gui_El_Style style, i32 x, i32 y, i32 w, i32 h, const char *text);
Vector3* gui_drawSizedEx(Gui_El_Style style, i32 x, i32 y, i32 w, i32 h, const char *text, u32 text_len);
Gui_Label gui_newLabel(i32 x, i32 y, char *text, Gui_El_Style defaultStyle, Gui_El_Style hovered);
Gui_Label gui_newCenteredLabel(Rectangle bounds, i32 w, char *text, Gui_El_Style defaultStyle, Gui_El_Style hovered);
void gui_centerLabel(Gui_Label *self, Rectangle bounds, i32 w);
void gui_rmCharLabel(Gui_Label *self, u32 idx);
void gui_insertCharLabel(Gui_Label *self, i32 idx, char c);
void gui_insertSliceLabel(Gui_Label *self, i32 idx, const char *slice, i32 slice_size);
Gui_El_State gui_getLabelState(Gui_Label self);
Vector2 gui_measureLabel(Gui_Label self, Gui_El_State state);
void gui_resizeLabel(Gui_Label *self, Gui_El_State state);
void gui_resizeLabelEx(Gui_Label *self, Gui_El_State state, char *text);
Gui_El_State gui_drawLabel(Gui_Label self);
Gui_Input_Box gui_newInputBox(char *placeholder, bool resize, bool multiline, bool selected, Gui_Label label);
bool gui_isInputBoxHovered(Gui_Input_Box self);
Gui_El_State gui_getInputBoxState(Gui_Input_Box *self);
Gui_El_State gui_getInputBoxStateHelper(Gui_Input_Box *self, bool hovered);
void gui_resetInputBoxAnim(Gui_Input_Box *self);
Gui_Update_Res gui_handleKeysInputBox(Gui_Input_Box *self);
Gui_Update_Res gui_drawInputBox(Gui_Input_Box *self);

#endif // GUI_H_


#ifdef GUI_IMPLEMENTATION

#include <string.h>
#include <stdbool.h>
#include <float.h>
#include "stb_ds.h"

// Static Variables
static i16   Input_Box_anim_len  = 50;   // Length of animation in ms
static i16   Input_Box_anim_wait = 30;   // Time in ms to wait after the cursor moved before starting to animate again
static i32   Input_Box_cur_width = 4;    // Width of the displayed cursor
static Color Input_Box_cur_color = { 0, 121, 241, 255 }; // Color of the displayed cursor
static i32   text_line_spacing   = 15;   // Same as in raylib;

inline void gui_setTextLineSpacing(i32 spacing)
{
    text_line_spacing = spacing;
    SetTextLineSpacing(spacing);
}

inline bool gui_stateIsActive(Gui_El_State state)
{
    return state >= EL_STATE_PRESSED;
}

inline bool gui_isPointInRec(i32 px, i32 py, i32 rx, i32 ry, i32 rw, i32 rh)
{
    return (px >= rx) && (px <= rx + rw) && (py >= ry) && (py <= ry + rh);
}

Gui_El_Style gui_defaultStyle(Font font)
{
    return (Gui_El_Style) {
        .bg           = BLANK,
        .border_color = BLANK,
        .border_width = 0,
        .color        = WHITE,
        .pad          = 0,
        .font         = font,
        .font_size    = 30,
        .spacing      = 0,
    };
}

Gui_El_Style gui_cloneStyle(Gui_El_Style self)
{
    return (Gui_El_Style) {
        .bg           = self.bg,
        .border_color = self.border_color,
        .border_width = self.border_width,
        .color        = self.color,
        .pad          = self.pad,
        .font         = self.font,
        .font_size    = self.font_size,
        .spacing      = self.spacing,
    };
}

void gui_drawSized(Gui_El_Style style, i32 x, i32 y, i32 w, i32 h, const char *text)
{
    if (style.border_width > 0) {
        DrawRectangle(x - style.border_width, y - style.border_width, w + 2*style.border_width, h + 2*style.border_width, style.border_color);
    }
    DrawRectangle(x, y, w, h, style.bg);
    if (text != NULL) DrawTextEx(style.font, text, (Vector2) { .x = (float)(x + style.pad), .y = (float)(y + style.pad) }, style.font_size, style.spacing, style.color);
}

/// Same as gui_drawSized, except it returns an array of coordinates for each byte in the drawn text
/// text_len is the amount of bytes in the text. It can be calculated with TextLength(text)
Vector3* gui_drawSizedEx(Gui_El_Style style, i32 x, i32 y, i32 w, i32 h, const char *text, u32 text_len)
{
    if (style.border_width > 0) {
        DrawRectangle(x - style.border_width, y - style.border_width, w + 2*style.border_width, h + 2*style.border_width, style.border_color);
    }
    DrawRectangle(x, y, w, h, style.bg);
    // Draw Text and compute result
    // Code for drawing text is adapted from DrawTextEx to return the glyphs' coordinates
    float xf = (float)(x + style.pad);
    float yf = (float)(y + style.pad);
    Vector3 *res = malloc(text_len * sizeof(Vector3));
    float textOffsetY = 0;                 // Offset between lines (on linebreak '\n')
    float textOffsetX = 0;                 // Offset X to next character to draw
    float scaleFactor = style.font_size/(float)(style.font.baseSize); // Character quad scaling factor
    u32 i = 0;
    while (i < text_len) {
        // Get next codepoint from byte string and glyph index in font
        i32 cp_byte_count = 0;
        i32 codepoint     = GetCodepointNext(&text[i], &cp_byte_count);
        u32 index         = GetGlyphIndex(style.font, codepoint);
        if (codepoint == '\n') {
            textOffsetY += (float)(text_line_spacing);
            textOffsetX = 0;
            res[i] = (Vector3) { .x = xf, .y = yf + textOffsetY, .z = 0 };
        } else {
            float advanceX = style.font.glyphs[index].advanceX;
            Vector3 v = { .x = xf + textOffsetX, .y = yf + textOffsetY, .z = advanceX };
            if (codepoint != ' ' && codepoint != '\t') {
                DrawTextCodepoint(style.font, codepoint, (Vector2){ .x=v.x, .y=v.y }, style.font_size, style.color);
            }
            if (advanceX == 0) {
                textOffsetX += style.font.recs[index].width*scaleFactor + style.spacing;
            } else {
                textOffsetX += advanceX*scaleFactor + style.spacing;
            }
            i32 j = 0;
            while (j < cp_byte_count) {
                res[i + j] = v;
                j += 1;
            }
        }

        i += cp_byte_count;   // Move text bytes counter to next codepoint
    }
    return res;
}

Gui_Label gui_newLabel(i32 x, i32 y, char *text, Gui_El_Style defaultStyle, Gui_El_Style hovered)
{
    Vector2 size     = MeasureTextEx(defaultStyle.font, text, defaultStyle.font_size, defaultStyle.spacing);
    char *arrList = NULL;
    i32 text_len  = text == NULL ? 0 : strlen(text);
    stbds_arrsetlen(arrList, text_len + 1);
    if (text_len > 0) memcpy(arrList, text, text_len + 1);
    arrList[text_len] = 0;

    return (Gui_Label) {
        .x            = x - defaultStyle.pad,
        .y            = y - defaultStyle.pad,
        .w            = ((i32) size.x) + 2*defaultStyle.pad,
        .h            = defaultStyle.font_size + 2*defaultStyle.pad,
        .text         = arrList,
        .defaultStyle = defaultStyle,
        .hovered      = hovered,
    };
}

void gui_centerLabel(Gui_Label *self, Rectangle bounds, i32 w)
{
    self->x = bounds.x + (bounds.width  - self->w)/2.0f;
    self->y = bounds.y + (bounds.height - self->h)/2.0f;
}

Gui_Label gui_newCenteredLabel(Rectangle bounds, i32 w, char *text, Gui_El_Style defaultStyle, Gui_El_Style hovered)
{
    char *extendable_text = NULL;
    i32 text_len          = text == NULL ? 0 : strlen(text);
    stbds_arrsetlen(extendable_text, text_len + 1);
    if (text_len > 0) memcpy(extendable_text, text, text_len + 1);
    extendable_text[text_len] = 0;

    i32 h = defaultStyle.font_size + 2*defaultStyle.pad;
    return (Gui_Label) {
        .x            = bounds.x + (bounds.width  - w)/2.0f,
        .y            = bounds.y + (bounds.height - h)/2.0f,
        .w            = w,
        .h            = h,
        .text         = extendable_text,
        .defaultStyle = defaultStyle,
        .hovered      = hovered,
    };
}

void gui_rmCharLabel(Gui_Label *self, u32 idx)
{
    if (idx >= stbds_arrlen(self->text)) return;
    stbds_arrdel(self->text, idx);
}

void gui_insertCharLabel(Gui_Label *self, i32 idx, char c)
{
    stbds_arrins(self->text, idx, c);
}

void gui_insertSliceLabel(Gui_Label *self, i32 idx, const char *slice, i32 slice_size)
{
    stbds_arrinsn(self->text, idx, slice_size);
    for (i32 i = 0; i < slice_size; i++) {
        self->text[idx + i] = slice[i];
    }
}

Gui_El_State gui_getLabelState(Gui_Label self)
{
    Vector2 mouse = GetMousePosition();
    bool hovered   = gui_isPointInRec((i32) mouse.x, (i32) mouse.y, self.x, self.y, self.w, self.h);
    if (hovered > 0) return (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ? EL_STATE_PRESSED : EL_STATE_HOVERED;
    else return EL_STATE_INACTIVE;
}

Vector2 gui_measureLabel(Gui_Label self, Gui_El_State state)
{
    Gui_El_Style style = (state >= EL_STATE_HOVERED) ? self.hovered : self.defaultStyle;
    return MeasureTextEx(style.font, self.text, style.font_size, style.spacing);
}

inline void gui_resizeLabel(Gui_Label *self, Gui_El_State state)
{
    gui_resizeLabelEx(self, state, self->text);
}

void gui_resizeLabelEx(Gui_Label *self, Gui_El_State state, char *text)
{
    Gui_El_Style style = (state >= EL_STATE_HOVERED) ? self->hovered : self->defaultStyle;
    Vector2 size  = MeasureTextEx(style.font, text, style.font_size, style.spacing);
    self->w = size.x + 2*style.pad;
    self->h = size.y + 2*style.pad;
}

Gui_El_State gui_drawLabel(Gui_Label self)
{
    Gui_El_State state = gui_getLabelState(self);
    if (state == EL_STATE_HIDDEN) return state;
    bool hovered = state >= EL_STATE_HOVERED;
    Gui_El_Style style   = (hovered) ? self.hovered : self.defaultStyle;
    gui_drawSized(style, self.x, self.y, self.w, self.h, self.text);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    return state;
}

Gui_Input_Box gui_newInputBox(char *placeholder, bool resize, bool multiline, bool selected, Gui_Label label)
{
    u16 rows = 1;
    u32 i = 0;
    while (i < stbds_arrlen(label.text)) {
        if (label.text[i] == '\n') rows += 1;
        i += 1;
    }

    return (Gui_Input_Box) {
        .placeholder = placeholder,
        .label       = label,
        .cur         = (stbds_arrlen(label.text) == 0) ? 0 : stbds_arrlen(label.text) - 1,
        .anim_idx    = 0,
        .rows        = rows,
        .resize      = resize,
        .multiline   = multiline,
        .selected    = selected,
    };
}

bool gui_isInputBoxHovered(Gui_Input_Box self)
{
    Vector2 mouse = GetMousePosition();
    return gui_isPointInRec((int)mouse.x, (int)mouse.y, self.label.x, self.label.y, self.label.w, self.label.h);
}

inline Gui_El_State gui_getInputBoxState(Gui_Input_Box *self)
{
    return gui_getInputBoxStateHelper(self, gui_isInputBoxHovered(*self));
}

Gui_El_State gui_getInputBoxStateHelper(Gui_Input_Box *self, bool hovered)
{
    bool EL_STATE_PRESSED = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (!hovered && EL_STATE_PRESSED) {
        self->selected = false;
        return EL_STATE_INACTIVE;
    } else if (EL_STATE_PRESSED) {
        self->selected = true;
        return EL_STATE_PRESSED;
    } else if (self->selected) {
        return EL_STATE_FOCUSED;
    } else if (hovered) {
        return EL_STATE_HOVERED;
    } else {
        return EL_STATE_INACTIVE;
    }
}

void gui_resetInputBoxAnim(Gui_Input_Box *self)
{
    self->anim_idx = -Input_Box_anim_len;
}

/// Returns true, if the label's text was updated in any way
Gui_Update_Res gui_handleKeysInputBox(Gui_Input_Box *self)
{
    Gui_Update_Res res = {0}; // @TODO: Default values

    if (IsKeyPressed(KEY_ENTER)) {
        if (self->multiline || (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT))) {
            gui_insertCharLabel(&self->label, self->cur, '\n');
            self->cur += 1;
            res.updated = true;
        } else {
            res.enter = true;
        }
    } else if (IsKeyPressed(KEY_TAB)) {
        res.tab = true;
    } else if (IsKeyPressed(KEY_RIGHT)) {
        if (self->cur < stbds_arrlen(self->label.text) - 1) self->cur += 1;
    } else if (IsKeyPressed(KEY_LEFT)) {
        if (self->cur > 0) self->cur -= 1;
    } else if (IsKeyPressed(KEY_UP)) {
        u32 prev_row = 0;
        u32 curr_row = 0;
        u32 i = 0;
        while (i < self->cur) {
            if (self->label.text[i] == '\n') {
                prev_row = curr_row;
                curr_row = i + 1;
            }
            i += 1;
        }
        if (curr_row == 0) {
            self->cur = 0;
        } else {
            u32 d = self->cur - curr_row;
            self->cur = (curr_row - prev_row > d) ? prev_row + d : curr_row - 1;
        }
    } else if (IsKeyPressed(KEY_DOWN)) {
        // @TODO
    } else if (IsKeyPressed(KEY_BACKSPACE)) {
        if (self->cur > 0) {
            gui_rmCharLabel(&self->label, self->cur - 1);
            self->cur -= 1;
            res.updated = true;
        }
    } else if (IsKeyPressed(KEY_DELETE)) {
        if (self->cur < stbds_arrlen(self->label.text) - 1) {
            gui_rmCharLabel(&self->label, self->cur);
            res.updated = true;
        }
    } else {
        i32 codepoint = GetCharPressed();
        if (codepoint > 0) {
            if (codepoint <= 0xff) {
                gui_insertCharLabel(&self->label, self->cur, (u8) codepoint);
                self->cur += 1;
            } else if (codepoint <= 0xffff) {
                char slice[] = {(u8)(codepoint & 0xff00), (u8)(codepoint & 0xff)};
                gui_insertSliceLabel(&self->label, self->cur, slice, 2);
                self->cur += 2;
            } else if (codepoint <= 0xffffff) {
                char slice[] = {(u8)(codepoint & 0xff0000), (u8)(codepoint & 0xff00), (u8)(codepoint & 0xff)};
                gui_insertSliceLabel(&self->label, self->cur, slice, 3);
                self->cur += 3;
            } else {
                char slice[] = {(u8)(codepoint & 0xff000000), (u8)(codepoint & 0xff0000), (u8)(codepoint & 0xff00), (u8)(codepoint & 0xff)};
                gui_insertSliceLabel(&self->label, self->cur, slice, 4);
                self->cur += 4;
            }
        }
        res.updated = true;
    }

    return res;
}

// Draws the Gui_Input_Box, but also does more, like handling any user input if the box is active
Gui_Update_Res gui_drawInputBox(Gui_Input_Box *self)
{
    Gui_Update_Res res = {0}; // @TODO: Default values
    bool hovered  = gui_isInputBoxHovered(*self);
    Gui_El_State state = gui_getInputBoxStateHelper(self, hovered);
    if (state == EL_STATE_HIDDEN) return res;
    Gui_El_Style style = (hovered || gui_stateIsActive(state)) ? self->label.hovered : self->label.defaultStyle;

    if (self->selected) {
        res = gui_handleKeysInputBox(self);
        if (res.updated && self->resize) gui_resizeLabel(&self->label, state);
    }
    u32 text_len   = stbds_arrlen(self->label.text) - 1;
    Vector3 *coords = gui_drawSizedEx(style, self->label.x, self->label.y, self->label.w, self->label.h, self->label.text, text_len);
    if (text_len == 0) {
        if (self->resize) {
            gui_resizeLabelEx(&self->label, state, self->placeholder);
        }
        u8 a = style.color.a;
        style.color.a = style.color.a/2.0f;
        gui_drawSized(style, self->label.x, self->label.y, self->label.w, self->label.h, self->placeholder);
        style.color.a = a;
    }

    // If mouse was clicked on the label, the cursor should be updated correspondingly
    if (state == EL_STATE_PRESSED) {
        Vector2 mouse  = GetMousePosition();
        u32 newCur     = 0;
        float distance = FLT_MAX;
        u16 rows       = 1;
        u32 i          = 0;
        while (i < text_len) {
            Vector3 c = coords[i];
            if (i + 1 == text_len && c.x < mouse.x) {
                newCur = text_len;
                break;
            } else if (mouse.x < c.x + c.z/2) {
                float d = mouse.y - c.y;
                if (d < 0) {
                    newCur = i;
                    break;
                } else if (d < distance) {
                    newCur   = i;
                    distance = d;
                    if (rows == self->rows) break;
                }
            }
            i += 1;
        }
        self->cur = newCur;
        self->anim_idx = -Input_Box_anim_wait;
    }

    // Display cursor
    {
        float anim_len_half = (float)(Input_Box_anim_len)/2.0f;
        i32 ai = (self->anim_idx < 0) ? 0 : self->anim_idx;
            ai = anim_len_half - ai;
        if (ai < 0) ai = -ai;
        float scale  = style.font_size;
        float height = scale * ((float)(ai))/anim_len_half;

        float x, y;
        if (text_len == 0) {
            x = (float)(self->label.x + style.pad);
            y = (float)(self->label.y + style.pad);
        } else if (self->cur < text_len) {
            x = coords[self->cur].x;
            y = coords[self->cur].y;
        } else {
            i32 cp_size    = 0;
            i32 cp         = GetCodepointNext(&self->label.text[text_len-1], &cp_size);
            float advanceX = (float)(GetGlyphInfo(style.font, cp).advanceX);
            x = coords[text_len-1].x + advanceX;
            y = coords[text_len-1].y;
        }

        x -= ((float) Input_Box_cur_width)/2.0f;
        y += (scale - height)/2.0f;
        DrawRectangle((i32) x, (i32) y, Input_Box_cur_width, (i32) height, Input_Box_cur_color);

        self->anim_idx += 1;
        if (self->anim_idx >= Input_Box_anim_len) self->anim_idx = 0;
    }

    free(coords);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    res.state = state;
    return res;
}

#endif // GUI_IMPLEMENTATION
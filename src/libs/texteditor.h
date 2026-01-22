#pragma once
#include "../common.h"

#include "linked_arena.h"
#include <cplug_extensions/window.h>
#include <imgui.h>
#include <nanovg2.h>
#include <stdbool.h>
#include <stdint.h>
#include <xhl/vector.h>

enum TextEditorAction
{
    TEXT_ACTION_MOVE,
    TEXT_ACTION_WRITE,
    TEXT_ACTION_DELETE,
};

typedef struct TextEditor
{
    NVGcontext* nvg;

    xvec4f dimensions;

    int* codepoints; // UTF32 format

    int selection_start;
    int selection_end;
    int ibeam_idx;

    float text_offset;

    float font_size;

    uint64_t time_last_ibeam_move;

    enum TextEditorAction last_action;
    LinkedArena*          undo_arena;
    // Nullable reference to data in the above arena.
    // Will be set to NULL on init
    struct TextEditorUndoState* ref_current_state;
} TextEditor;

void ted_init(TextEditor*, NVGcontext*);
void ted_deinit(TextEditor*);

void ted_activate(TextEditor* ted, xvec4f dimensions, xvec2f pos, float _font_size, const char* text);
void ted_deactivate(TextEditor* ted);

// Returns true if event is consumed/used by our app
bool ted_handle_key_down(TextEditor* ted, void* pw, const PWEvent* event);
void ted_handle_text(TextEditor* ted, int codepoint); // utf32 codepoint
void ted_handle_mouse_down(TextEditor* ted, imgui_context* im);
void ted_handle_mouse_drag(TextEditor* ted, imgui_context* im);

// Returns number of bytes copied into 'buf', excluding the NULL terminating byte
size_t ted_get_text(TextEditor*, char* buf, size_t buflen);
void   ted_clear(TextEditor*);

void ted_draw(
    TextEditor* ted,
    uint64_t    frame_time_ns,
    NVGcolour   col_text,
    NVGcolour   col_selection_bg,
    NVGcolour   col_ibeam);
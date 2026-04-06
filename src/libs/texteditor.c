#include "texteditor.h"
#include <imgui.h>
#include <stdbool.h>
#include <utf8.h>
#include <xhl/array2.h>
#include <xhl/maths.h>
#include <xhl/time.h>

// Doubly linked list stack. of state items
typedef struct TextEditorUndoState
{
    size_t size_bytes;

    struct TextEditorUndoState* prev;
    struct TextEditorUndoState* next;

    int selection_start;
    int selection_end;
    int ibeam_idx;

    uint32_t text_len;

    int text[];
} TextEditorUndoState;

void ted_push_state(TextEditor* ted)
{
    if (ted->ref_current_state && ted->ref_current_state->next)
    {
        linked_arena_release(ted->undo_arena, ted->ref_current_state->next);
        ted->ref_current_state->next = NULL;
    }

    size_t size_bytes = sizeof(TextEditorUndoState);
    size_t text_len   = xarr_len(ted->codepoints);

    size_bytes += text_len * sizeof(*ted->codepoints);

    TextEditorUndoState* state = linked_arena_alloc(ted->undo_arena, size_bytes);

    state->size_bytes      = size_bytes;
    state->prev            = ted->ref_current_state;
    state->next            = NULL;
    state->selection_start = ted->selection_start;
    state->selection_end   = ted->selection_end;
    state->ibeam_idx       = ted->ibeam_idx;
    state->text_len        = text_len;
    memcpy(state->text, ted->codepoints, text_len * sizeof(*ted->codepoints));

    if (ted->ref_current_state)
    {
        // Test the state we're about to push is actually new
        if (ted->ref_current_state->size_bytes == size_bytes)
        {
            const bool is_duplicate = 0 == memcmp(state, ted->ref_current_state, size_bytes);
            xassert(!is_duplicate);
            if (is_duplicate)
            {
                println("[Warning] Trying to push duplicate state to Undo arena. Aborting!");
                linked_arena_release(ted->undo_arena, state);
                return;
            }
        }
        ted->ref_current_state->next = state;
    }
    ted->ref_current_state = state;

    // #ifndef NDEBUG
    //     char text[128] = {0};
    //     if (text_len)
    //         ted_get_text(ted, text, sizeof(text));
    //     println("Pushed state! - %s", text);
    // #endif
}

void ted_set_action(TextEditor* ted, enum TextEditorAction next_action)
{
    const enum TextEditorAction last_action = ted->last_action;
    if (next_action != last_action)
    {
        // static const char* names[] = {
        //     "TEXT_ACTION_MOVE",
        //     "TEXT_ACTION_WRITE",
        //     "TEXT_ACTION_DELETE",
        // };
        // println("Updating action from %s > %s", names[last_action], names[next_action]);
        ted->last_action = next_action;
        if (last_action != TEXT_ACTION_MOVE)
            ted_push_state(ted);
    }
}

// Returns number of bytes written to buf, excluding null terminating byte
size_t ted_get_text_range(TextEditor* ted, char* buf, size_t buflen, const int* it, const int* const end)
{
    xassert(buflen > 0);
    xassert(buflen > (end - it));

    char*             c     = buf;
    const char* const c_end = buf + buflen - 1;
    // convert utf32 to utf8
    while (it != end && buf)
    {
        c = utf8catcodepoint(c, *it, c_end - c);
        it++;
    }
    xassert((c_end - c) < buflen);
    *c = 0;

    return c - buf;
}

/* Maybe usefull?
void ted_alloc_text(TextEditor* ted, char** out_text, size_t* out_text_len, size_t* out_num_chars)
{
    char*  alloc_text     = NULL;
    size_t alloc_text_len = 0;
    size_t total_chars    = xarr_len(ted->codepoints);

    if (total_chars)
    {
        size_t alloc_size = 4 * total_chars + 1;
        alloc_text        = linked_arena_alloc(ted->undo_arena, alloc_size);
        alloc_text_len    = ted_get_text(ted, alloc_text, alloc_size);
    }
    *out_text      = alloc_text;
    *out_text_len  = alloc_text_len;
    *out_num_chars = total_chars;
}

void ted_text_release(TextEditor* ted, const char* text) { linked_arena_release(ted->undo_arena, text); }
*/

// Moves visible text offset to fit ibeam. Handles pushing new undo/redo state
void ted_handle_ibeam_moved(TextEditor* ted)
{
    LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(ted->undo_arena, _ted_arena);

    // Check if cursor has moved out of visible bounds
    size_t total_chars = xarr_len(ted->codepoints);

    if (total_chars)
    {
        LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(ted->xvg->arena, _nvg_arena);

        size_t alloc_size = 4 * total_chars + 1;
        char*  alloc_text = linked_arena_alloc(ted->undo_arena, alloc_size);
        size_t text_len   = ted_get_text(ted, alloc_text, alloc_size);

        const XVGTextLayout* layout =
            xvg_create_text_layout(ted->xvg, alloc_text, alloc_text + text_len, ted->theme.font_size, 0, 1);
        const XVGGlyphLayout* glyphs     = xvg_layout_get_glyphs(layout);
        float                 text_width = layout->xmax;

        float ibeam_x = ted->ibeam_idx < layout->num_glyphs ? glyphs[ted->ibeam_idx].x : text_width;
        if (ibeam_x < 0)
            ibeam_x = 0;

        ibeam_x += ted->text_offset;

        // Centre alignment
        if (ted->theme.is_centre_aligned)
        {
            ibeam_x += ted->dimensions.width * 0.5f;
            ibeam_x -= text_width * 0.5f;
        }

        const float padding      = 2; // We need 2 px for the ibeam
        const float margin_right = ted->dimensions.width - padding - ted->theme.padding_left;
        if (ibeam_x > margin_right) // IBeam moved out of right bounds
        {
            float diff        = ibeam_x - margin_right;
            ted->text_offset -= diff;
        }
        else if (ibeam_x < 0) // IBeam moved out of left bounds
        {
            ted->text_offset = xm_minf(0, ted->text_offset - ibeam_x);
        }
        else if (ted->ibeam_idx == total_chars && ted->text_offset < 0) // User is deleting the last character
        {
            xassert(ibeam_x >= 0 && ibeam_x <= margin_right);
            float diff       = margin_right - ibeam_x;
            ted->text_offset = xm_minf(0, ted->text_offset + diff);
        }

        xassert(ted->text_offset <= 0);

        xvg_release_text_layout(ted->xvg, layout);
        linked_arena_release(ted->undo_arena, alloc_text);

        LINKED_ARENA_TAGGED_LEAK_DETECT_END(ted->xvg->arena, _nvg_arena);
    }

    ted->time_last_ibeam_move = xtime_now_ns();

    LINKED_ARENA_TAGGED_LEAK_DETECT_END(ted->undo_arena, _ted_arena);
}

void ted_delete_selection(TextEditor* ted)
{
    xassert(ted->selection_end >= ted->selection_start);
    size_t diff = ted->selection_end - ted->selection_start;
    if (diff)
    {
        ted_set_action(ted, TEXT_ACTION_DELETE);
        xarr_deleten(ted->codepoints, ted->selection_start, diff);

        if (ted->ibeam_idx >= ted->selection_start && ted->ibeam_idx <= ted->selection_end)
        {
            ted->ibeam_idx = ted->selection_start;
        }
        ted->selection_end = ted->selection_start;
    }
    ted->selection_start = 0;
    ted->selection_end   = 0;
}

bool ted_handle_copy(TextEditor* ted, void* pw)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(ted->undo_arena)

    const size_t num_characters = ted->selection_end - ted->selection_start;
    if (num_characters)
    {
        size_t buf_len = 4 * num_characters + 1;
        char*  buf     = linked_arena_alloc(ted->undo_arena, buf_len);

        const int* range_start = ted->codepoints + ted->selection_start;
        const int* range_end   = ted->codepoints + ted->selection_end;

        ted_get_text_range(ted, buf, buf_len, range_start, range_end);
        pw_set_clipboard_text(pw, buf);
        linked_arena_release(ted->undo_arena, buf);
    }
    LINKED_ARENA_LEAK_DETECT_END(ted->undo_arena)
    return true;
}

bool ted_handle_cut(TextEditor* ted, void* pw)
{
    ted_handle_copy(ted, pw);
    ted_delete_selection(ted);
    ted_handle_ibeam_moved(ted);
    return true;
}

bool ted_handle_paste(TextEditor* ted, void* pw)
{
    char*  buf    = NULL;
    size_t buflen = 0;

    pw_get_clipboard_text(pw, &buf, &buflen);
    if (buf && buflen)
    {
        ted_delete_selection(ted);

        // insert text at range
        int*   new_codepoints     = linked_arena_alloc(ted->undo_arena, buflen * 4);
        size_t num_new_codepoints = 0;

        char* buf_it = buf;
        while (*buf_it != 0)
        {
            buf_it = utf8codepoint(buf_it, new_codepoints + num_new_codepoints);
            num_new_codepoints++;
        }

        const size_t num_current_codepoints = xarr_len(ted->codepoints);
        const size_t required_cap           = num_new_codepoints + num_current_codepoints;
        xarr_setcap(ted->codepoints, required_cap);

        void* arr_insert_pos = (void*)(ted->codepoints + ted->ibeam_idx);
        void* arr_end        = (void*)(ted->codepoints + num_current_codepoints);

        size_t cpy_mem_size  = num_new_codepoints * sizeof(int);
        size_t move_mem_size = arr_end - arr_insert_pos;
        if (move_mem_size)
            memmove(arr_insert_pos + cpy_mem_size, arr_insert_pos, move_mem_size);
        memcpy(arr_insert_pos, new_codepoints, cpy_mem_size);

        linked_arena_release(ted->undo_arena, new_codepoints);
        pw_free_clipboard_text(buf);

        xarr_header(ted->codepoints)->length += num_new_codepoints;
        ted->ibeam_idx                       += num_new_codepoints;
        ted_handle_ibeam_moved(ted);
    }
    return true;
}

void ted_apply_state(TextEditor* ted, TextEditorUndoState* next_state)
{
    ted->selection_start = next_state->selection_start;
    ted->selection_end   = next_state->selection_end;
    ted->ibeam_idx       = next_state->ibeam_idx;
    xarr_setlen(ted->codepoints, next_state->text_len);
    memcpy(ted->codepoints, next_state->text, sizeof(*ted->codepoints) * next_state->text_len);
    ted->ref_current_state = next_state;
    ted_handle_ibeam_moved(ted);

    // #ifndef NDEBUG
    //     char text[128] = {0};
    //     if (next_state->text_len)
    //         ted_get_text(ted, text, sizeof(text));
    //     println("Applied state! - %s", text);
    // #endif
}

bool ted_handle_undo(TextEditor* ted)
{
    if (ted->ref_current_state && ted->ref_current_state->prev)
    {
        ted_set_action(ted, TEXT_ACTION_MOVE);
        TextEditorUndoState* next_state = ted->ref_current_state->prev;
        ted_apply_state(ted, next_state);
    }
    return true;
}

bool ted_handle_redo(TextEditor* ted)
{
    if (ted->last_action == TEXT_ACTION_MOVE && ted->ref_current_state && ted->ref_current_state->next)
    {
        TextEditorUndoState* next_state = ted->ref_current_state->next;
        ted_apply_state(ted, next_state);
    }
    return true;
}

void ted_set_ibeam_idx(TextEditor* ted, int idx, bool selecting)
{
    xassert(ted->selection_start <= ted->selection_end);
    if (selecting)
    {
        xassert(
            (ted->selection_start == ted->selection_end && ted->selection_start == 0) ||
            (ted->selection_end > 0 &&
             (ted->ibeam_idx == ted->selection_start || ted->ibeam_idx == ted->selection_end)));

        int a = 0;
        if (ted->selection_start == ted->selection_end) // no selection
            a = ted->ibeam_idx;
        else if (ted->ibeam_idx == ted->selection_start) // Move left selection boundary
            a = ted->selection_end;
        else if (ted->ibeam_idx == ted->selection_end) // Move left selection boundary
            a = ted->selection_start;
        ted->selection_start = xm_mini(a, idx);
        ted->selection_end   = xm_maxi(a, idx);
    }
    else
    {
        ted->selection_start = 0;
        ted->selection_end   = 0;
    }

    xassert(ted->selection_start <= ted->selection_end);
    ted->ibeam_idx = idx;

    ted_handle_ibeam_moved(ted);
}

// Ignore C locale nonsense. Just go by ISO space characters
// All ANSI and UTF16 space characters are identical to UTF32
bool isspace_utf16(uint16_t cp)
{
    // https://www.open-std.org/JTC1/SC35/WG5/docs/30112d10.pdf, p.30
    static const uint16_t list[] = {
        // https://en.cppreference.com/w/c/string/byte/isspace
        0x20, // Space (' ')
        0x09, // Horizontal tab ('\t')
        0x0a, // Line feed ('\n')
        0x0b, // Vertical tab ('\v')
        0x0c, // Form feed ('\f')
        0x0d, // Carriage return ('\r')

        // https://en.cppreference.com/w/c/string/wide/iswspace
        0x1680, // OGHAM SPACE MARK
        0x180E, // MONGOLIAN VOWEL SEPARATOR
        0x2000, // EN QUAD
        0x2001, // EM QUAD
        0x2002, // EN SPACE
        0x2003, // EM SPACE
        0x2004, // THREE-PER-EM SPACE
        0x2005, // FOUR-PER-EM SPACE
        0x2006, // SIX-PER-EM SPACE
        0x2008, // PUNCTUATION SPACE
        0x2009, // THIN SPACE
        0x200a, // HAIR SPACE
        0x2028, // LINE SEPARATOR
        0x2029, // PARAGRAPH SEPARATOR
        0x205F, // MEDIUM MATHEMATICAL SPACE
        0x3000, // IDEOGRAPHIC SPACE
    };
    for (int i = 0; i < ARRLEN(list); i++)
        if (cp == list[i])
            return true;
    return false;
}

// Ignore C locale nonsense. Just go by ISO space characters
// This is likely much slower than standard libc
bool ispunct_utf32(uint32_t cp)
{
    // clang-format off
    // https://www.open-std.org/JTC1/SC35/WG5/docs/30112d10.pdf, p.30
    static const uint16_t singles_utf16[] = {
        0x00D7, 0x00F7, 0x037E, 0x0387, 0x03F6, 0x0670, 0x06D4, 0x070F, 0x0711, 0x0964, 0x0965,
        0x0E2F, 0x0E3F, 0x0E46, 0x0E4F, 0x0EB1, 0x10FB, 0x17DD, 0x18A9, 0x1940, 0x109E, 0x109F,
        0x1FBD, 0x2007, 0x2114, 0x2125, 0x2127, 0x212E, 0x274D, 0x2756, 0x27CC, 0x30A0, 0x30FB,
        0xA802, 0xA806, 0xA80B, 0xA880, 0xA881, 0xA95F, 0xAA43, 0xFB1E, 0xFB29, 0xFEFF,
    };
    for (int i = 0; i < ARRLEN(singles_utf16); i++)
        if (cp == singles_utf16[i])
            return true;

    static const struct
    {
        uint16_t start, end;
    } ranges_utf16[] = {
        {0x0021, 0x002F}, {0x003A, 0x0040}, {0x005B, 0x0060}, {0x007B, 0x007E}, {0x00A0, 0x00A9}, {0x00AB, 0x00B4},
        {0x00B6, 0x00B9}, {0x00BB, 0x00BF}, {0x02C2, 0x02C5}, {0x02D2, 0x02DF}, {0x02E5, 0x02ED}, {0x02EF, 0x0344},
        {0x0346, 0x036F}, {0x0374, 0x0375}, {0x0384, 0x0385}, {0x0482, 0x0486}, {0x0488, 0x0489}, {0x055A, 0x055F},
        {0x0589, 0x058A}, {0x0591, 0x05C7}, {0x05F3, 0x05F4}, {0x0600, 0x0603}, {0x060B, 0x061B}, {0x061E, 0x061F},
        {0x064B, 0x065E}, {0x066A, 0x066D}, {0x06D6, 0x06E4}, {0x06E7, 0x06ED}, {0x06FD, 0x06FE}, {0x0700, 0x070D},
        {0x0730, 0x074A}, {0x07A6, 0x07B0}, {0x07EB, 0x07F3}, {0x07F6, 0x07F9}, {0x0E5A, 0x0E5B}, {0x0EB4, 0x0EB9},
        {0x0EBB, 0x0EBC}, {0x0EC8, 0x0ECD}, {0x0F01, 0x0F1F}, {0x0F2A, 0x0F3F}, {0x0F71, 0x0F87}, {0x0F90, 0x0F97},
        {0x0F99, 0x0FBC}, {0x0FBE, 0x0FCC}, {0x0FCE, 0x0FD4}, {0x102B, 0x103F}, {0x104A, 0x104F}, {0x1056, 0x1059},
        {0x105E, 0x1060}, {0x1062, 0x1064}, {0x1067, 0x106D}, {0x1071, 0x1074}, {0x1082, 0x108D}, {0x108F, 0x1099},
        {0x135F, 0x137C}, {0x1390, 0x1399}, {0x166D, 0x166E}, {0x169B, 0x169C}, {0x16EB, 0x16ED}, {0x1712, 0x1714},
        {0x1732, 0x1736}, {0x1752, 0x1753}, {0x1772, 0x1773}, {0x17B4, 0x17D6}, {0x17D8, 0x17DB}, {0x17F0, 0x17F9},
        {0x1800, 0x180D}, {0x1920, 0x192B}, {0x1930, 0x193B}, {0x1944, 0x1945}, {0x19B0, 0x19C0}, {0x19C8, 0x19C9},
        {0x19DE, 0x19FF}, {0x1A17, 0x1A1B}, {0x1A1E, 0x1A1F}, {0x1B00, 0x1B04}, {0x1B34, 0x1B44}, {0x1B5A, 0x1B7C},
        {0x1B80, 0x1B82}, {0x1BA1, 0x1BAA}, {0x1C24, 0x1C37}, {0x1C3B, 0x1C3F}, {0x1C7E, 0x1C7F}, {0x1DC0, 0x1DE6},
        {0x1DFE, 0x1DFF}, {0x1FBF, 0x1FC1}, {0x1FCD, 0x1FCF}, {0x1FDD, 0x1FDF}, {0x1FED, 0x1FEF}, {0x1FFD, 0x1FFE},
        {0x200B, 0x2027}, {0x202A, 0x205E}, {0x2060, 0x2064}, {0x206A, 0x2070}, {0x2074, 0x207E}, {0x2080, 0x208E},
        {0x20A0, 0x20B5}, {0x20D0, 0x20F0}, {0x2100, 0x2101}, {0x2103, 0x2106}, {0x2108, 0x2109}, {0x2116, 0x2118},
        {0x211E, 0x2123}, {0x213A, 0x213B}, {0x2140, 0x2144}, {0x214A, 0x214D}, {0x2153, 0x215F}, {0x2190, 0x23E7},
        {0x2400, 0x2426}, {0x2440, 0x244A}, {0x2460, 0x249B}, {0x24EA, 0x269D}, {0x26A0, 0x26C3}, {0x2701, 0x2704},
        {0x2706, 0x2709}, {0x270C, 0x2727}, {0x2729, 0x274B}, {0x274F, 0x2752}, {0x2758, 0x275E}, {0x2761, 0x2794},
        {0x2798, 0x27AF}, {0x27B1, 0x27BE}, {0x27C0, 0x27CA}, {0x27D0, 0x27EF}, {0x27F0, 0x2B4C}, {0x2B50, 0x2B54},
        {0x2DE0, 0x2DFF}, {0x2CE5, 0x2CEA}, {0x2CF9, 0x2CFF}, {0x2E00, 0x2E30}, {0x2E80, 0x2E99}, {0x2E9B, 0x2EF3},
        {0x2F00, 0x2FD5}, {0x2FF0, 0x2FFB}, {0x3001, 0x3004}, {0x3008, 0x3020}, {0x302A, 0x3030}, {0x3036, 0x3037},
        {0x303D, 0x303F}, {0x3099, 0x309C}, {0x3190, 0x319F}, {0x31C0, 0x31CF}, {0x3200, 0x321E}, {0x3220, 0x3243},
        {0x3250, 0x32FE}, {0x3300, 0x33FF}, {0x4DC0, 0x4DFF}, {0xA490, 0xA4C6}, {0xA60C, 0xA60F}, {0xA66F, 0xA673},
        {0xA67C, 0xA67F}, {0xA700, 0xA716}, {0xA720, 0xA721}, {0xA823, 0xA82B}, {0xA874, 0xA877}, {0xA8B4, 0xA8C4},
        {0xA8CE, 0xA8CF}, {0xA92E, 0xA92F}, {0xA947, 0xA953}, {0xAA29, 0xAA36}, {0xAA4C, 0xAA4D}, {0xAA5C, 0xAA5F},
        {0xE000, 0xF8FF}, {0xFD3E, 0xFD3F}, {0xFDFC, 0xFDFD}, {0xFE00, 0xFE19}, {0xFE20, 0xFE26}, {0xFE30, 0xFE52},
        {0xFE54, 0xFE66}, {0xFE68, 0xFE6B}, {0xFF01, 0xFF0F}, {0xFF1A, 0xFF20}, {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65},
        {0xFFE0, 0xFFE6}, {0xFFE8, 0xFFEE}, {0xFFF9, 0xFFFD},
    };
    for (int i = 0; i < ARRLEN(ranges_utf16); i++)
        if (cp >= ranges_utf16[i].start && cp <= ranges_utf16[i].end)
            return true;

    static const uint32_t singles_utf32[] = {
        0x0001039F, 0x000103D0, 0x0001091F, 0x0001D6C1, 0x0001D6DB, 0x0001D6FB, 0x0001D715,
        0x0001D735, 0x0001D74F, 0x0001D76F, 0x0001D789, 0x0001D7A9, 0x0001D7C3, 0x000E0001,
    };
    for (int i = 0; i < ARRLEN(singles_utf32); i++)
        if (cp == singles_utf32[i])
            return true;

    static const struct
    {
        uint32_t start, end;
    } ranges_utf32[] = {
        {0x00010100, 0x00010102}, {0x00010107, 0x00010133}, {0x00010137, 0x0001013F}, {0x00010175, 0x0001018A},
        {0x00010320, 0x00010323}, {0x00010916, 0x00010919}, {0x00010A01, 0x00010A03}, {0x00010A05, 0x00010A06}, 
        {0x00010A0C, 0x00010A0F}, {0x00010A38, 0x00010A3A}, {0x00010A3F, 0x00010A47}, {0x00010A50, 0x00010A58}, 
        {0x00012470, 0x00012473}, {0x0001D000, 0x0001D0F5}, {0x0001D100, 0x0001D126}, {0x0001D129, 0x0001D1DD}, 
        {0x0001D200, 0x0001D245}, {0x0001D300, 0x0001D356}, {0x0001D360, 0x0001D371}, {0x000E0020, 0x000E007F},
        {0x000E0100, 0x000E01EF}, {0x000F0000, 0x000FFFFD}, {0x00100000, 0x0010FFFD},
    };
    for (int i = 0; i < ARRLEN(ranges_utf32); i++)
        if (cp >= ranges_utf32[i].start && cp <= ranges_utf32[i].end)
            return true;
    // clang-format on

    return false;
}

bool isbreakpunct(uint32_t cp)
{
    if (cp == '\'' || cp == '_') // Whitelist punct chars
        return false;
    return ispunct_utf32(cp);
}

// When jumping between words (eg. ctrl + left/right), we need to know what kind of characters to skip, and what kinds
// we stop at. For example if the position of the ibeam is at an alphanumberic character, then we skip all other
// alphanumeric charcters until we hit a different charatcer 'type', like a space or punctuation charcter. The same
// logic applies to the other types, so if the ibeam is at a punctuation character, other punctualtion chars should be
// skipped, and it should break when it reaches other type.
// One caveat is how macOS and Windows handle breaking around spaces. macOS will often break as soon as it hits a space,
// where as Windows will often skip the spaces.
// This is modelled off of the UX in Google Chrome with their <input type="text" /> tags.
enum TextEditorCharType
{
    CHAR_TYPE_SPACE,
    CHAR_TYPE_PUNCT,
    CHAR_TYPE_ALPHANUMERIC,
};

enum TextEditorCharType ted_get_char_type(int cp)
{
    if (isspace_utf16(cp))
        return CHAR_TYPE_SPACE;
    if (isbreakpunct(cp))
        return CHAR_TYPE_PUNCT;
    return CHAR_TYPE_ALPHANUMERIC;
}

int ted_get_word_index_left(TextEditor* ted)
{
    int idx = ted->ibeam_idx;
    while (idx > 0 && isspace_utf16(ted->codepoints[idx - 1]))
        idx--;

    if (idx > 0)
    {
        idx--;
        enum TextEditorCharType type = ted_get_char_type(ted->codepoints[idx]);
        while (idx > 0 && type == ted_get_char_type(ted->codepoints[idx - 1]))
            idx--;
    }

    return idx;
}

int ted_get_word_index_right(TextEditor* ted)
{
    int idx = ted->ibeam_idx;
    int end = xarr_len(ted->codepoints);

    if (idx < end)
    {
        enum TextEditorCharType type = ted_get_char_type(ted->codepoints[idx]);
        idx++;
        while (idx < end && type == ted_get_char_type(ted->codepoints[idx]))
            idx++;
#ifdef _WIN32
        while (idx < end && isspace_utf16(ted->codepoints[idx]))
            idx++;
#endif
    }

    return idx;
}

bool ted_handle_move_ibeam_left(TextEditor* ted, bool jump_line, bool jump_word, bool selecting)
{
    ted_set_action(ted, TEXT_ACTION_MOVE);
    int next_idx;
    if (jump_line)
        next_idx = 0;
    else if (jump_word)
        next_idx = ted_get_word_index_left(ted);
    else if (!selecting && ted->selection_start != ted->selection_end)
        next_idx = ted->selection_start;
    else
        next_idx = xm_maxi(0, ted->ibeam_idx - 1);

    ted_set_ibeam_idx(ted, next_idx, selecting);
    return true;
}

bool ted_handle_move_ibeam_right(TextEditor* ted, bool jump_line, bool jump_word, bool selecting)
{
    ted_set_action(ted, TEXT_ACTION_MOVE);
    int next_idx;
    if (jump_line)
        next_idx = xarr_len(ted->codepoints);
    else if (jump_word)
        next_idx = ted_get_word_index_right(ted);
    else if (!selecting && ted->selection_start != ted->selection_end)
        next_idx = ted->selection_end;
    else
        next_idx = xm_mini(xarr_len(ted->codepoints), ted->ibeam_idx + 1);
    ted_set_ibeam_idx(ted, next_idx, selecting);
    return true;
}

// NOTE: Jump line takes precedence over jump word.
// Creating some enum type to handle the correct jump logic is probably not necessary
bool ted_handle_backspace(TextEditor* ted, bool jump_line, bool jump_word)
{
    if (ted->selection_start != ted->selection_end)
    {
        ted_delete_selection(ted);
    }
    else if (ted->ibeam_idx > 0)
    {
        ted_set_action(ted, TEXT_ACTION_DELETE);
        int start;
        if (jump_line)
            start = 0;
        else if (jump_word)
            start = ted_get_word_index_left(ted);
        else
            start = ted->ibeam_idx - 1;

        xassert(start < ted->ibeam_idx);
        int n = ted->ibeam_idx - start;
        xarr_deleten(ted->codepoints, start, n);

        ted->ibeam_idx -= n;
    }
    ted_handle_ibeam_moved(ted);
    return true;
}

bool ted_handle_delete(TextEditor* ted, bool jump_word)
{
    if (ted->selection_start != ted->selection_end)
    {
        ted_delete_selection(ted);
    }
    else if (ted->ibeam_idx < xarr_len(ted->codepoints))
    {
        ted_set_action(ted, TEXT_ACTION_DELETE);
        int end;
        if (jump_word)
            end = ted_get_word_index_right(ted);
        else
            end = ted->ibeam_idx + 1;

        xassert(end > ted->ibeam_idx);
        int n = end - ted->ibeam_idx;
        xarr_deleten(ted->codepoints, ted->ibeam_idx, n);
    }
    ted_handle_ibeam_moved(ted);
    return true;
}

// Returns true if event is consumed/used by our app
// Behaviour modeled off of playing in Google Chrome HTML <input>
// https://www.w3schools.com/tags/tryit.asp?filename=tryhtml_input_test
bool ted_handle_key_down(TextEditor* ted, void* pw, const PWEvent* event)
{
    const enum PWVirtualKey key  = event->key.virtual_key;
    const uint32_t          mods = event->key.modifiers;

    const bool ctrl_cmd = mods & PW_MOD_PLATFORM_KEY_CTRL;
    const bool alt_opt  = mods & PW_MOD_PLATFORM_KEY_ALT;
    const bool shift    = mods & PW_MOD_KEY_SHIFT;

#ifdef _WIN32
    const bool jump_word = ctrl_cmd;
    const bool jump_line = false; // Does't seem to be possible on Windows
#elif defined(__APPLE__)
    const bool jump_word = alt_opt;
    const bool jump_line = ctrl_cmd;
#else
#error Unsupported platform
#endif

    // No key combination seems to work if more than 2 modifiers are held
    if (xm_popcountu(mods) > 2) // ctrl/cmd/alt/shift
        return false;

    if (ctrl_cmd)
    {
        if (key == PW_KEY_A) // select all
        {
            ted->selection_start = 0;
            ted->ibeam_idx = ted->selection_end = xarr_len(ted->codepoints);
            ted_handle_ibeam_moved(ted);
            return true;
        }
        if (key == PW_KEY_C)
            return ted_handle_copy(ted, pw);
        if (key == PW_KEY_X)
            return ted_handle_cut(ted, pw);
        if (key == PW_KEY_V)
            return ted_handle_paste(ted, pw);
        if (key == PW_KEY_Y)
            return ted_handle_redo(ted);
        if (key == PW_KEY_Z && shift)
            return ted_handle_redo(ted);
        if (key == PW_KEY_Z)
            return ted_handle_undo(ted);
    }

#ifdef _WIN32
    if (key == PW_KEY_DELETE && mods == PW_MOD_KEY_SHIFT) // I cannot find a mac equivalent of this
        return ted_handle_cut(ted, pw);
    if (key == PW_KEY_DELETE)
        return ted_handle_delete(ted, mods == PW_MOD_KEY_CTRL);
#endif

    if (key == PW_KEY_ARROW_LEFT)
        return ted_handle_move_ibeam_left(ted, jump_line, jump_word, shift);
    if (key == PW_KEY_ARROW_RIGHT)
        return ted_handle_move_ibeam_right(ted, jump_line, jump_word, shift);
    if (key == PW_KEY_ARROW_UP)
        return ted_handle_move_ibeam_left(ted, true, false, shift);
    if (key == PW_KEY_ARROW_DOWN)
        return ted_handle_move_ibeam_right(ted, true, false, shift);

    if (key == PW_KEY_BACKSPACE)
        return ted_handle_backspace(ted, jump_line, jump_word);
    if (key == PW_KEY_HOME && !alt_opt)
        return ted_handle_move_ibeam_left(ted, true, false, shift);
    if (key == PW_KEY_END && !alt_opt)
        return ted_handle_move_ibeam_right(ted, true, false, shift);
    if (key == PW_KEY_INSERT && mods == PW_MOD_KEY_SHIFT)
        return ted_handle_paste(ted, pw);
    if (key == PW_KEY_INSERT && mods == PW_MOD_KEY_CTRL) // ctrl on mac (not cmd!) AND ctrl on windows
        return ted_handle_copy(ted, pw);

    // Keypress not used. eg. PW_KEY_F1 - F12

    return false;
}

void ted_handle_text(TextEditor* ted, int cp)
{
    ted_set_action(ted, TEXT_ACTION_WRITE);
    ted_delete_selection(ted);
    xassert(ted->selection_start == ted->selection_end);

    xarr_insert(ted->codepoints, ted->ibeam_idx, cp);
    ted->ibeam_idx++;
    ted_handle_ibeam_moved(ted);
}

int ted_get_text_idx(TextEditor* ted, float x)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(ted->undo_arena)

    int idx = 0;

    size_t total_chars = xarr_len(ted->codepoints);
    if (total_chars)
    {
        LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(ted->xvg->arena, _nvg_arena);
        size_t alloc_size = 4 * total_chars + 1;
        char*  alloc_text = linked_arena_alloc(ted->undo_arena, alloc_size);
        size_t text_len   = ted_get_text(ted, alloc_text, alloc_size);

        const XVGTextLayout* layout =
            xvg_create_text_layout(ted->xvg, alloc_text, alloc_text + text_len, ted->theme.font_size, 0, 1);
        const XVGGlyphLayout* glyphs = xvg_layout_get_glyphs(layout);

        float text_width = layout->xmax;

        // Centre alignment
        if (ted->theme.is_centre_aligned)
        {
            x -= ted->dimensions.width * 0.5f;
            x += text_width * 0.5f;
        }
        else
        {
            x -= ted->theme.padding_left;
        }

        x -= ted->dimensions.x;
        x -= ted->text_offset;

        x *= ted->xvg->xvg->backingScaleFactor;
        if (x >= 0)
        {
            while (idx < layout->num_glyphs - 1 && !(x >= glyphs[idx].x && x <= glyphs[idx + 1].x))
                idx++;
        }

        // Snap to closest index
        float diff_min = x - glyphs[idx].x;
        float diff_max = (glyphs[idx].x + glyphs[idx].rect.w) - x;
        if (idx < total_chars && diff_min > diff_max)
            idx++;

        xassert(idx >= 0 && idx <= xarr_len(ted->codepoints));

        xvg_release_text_layout(ted->xvg, layout);
        linked_arena_release(ted->undo_arena, alloc_text);
        LINKED_ARENA_TAGGED_LEAK_DETECT_END(ted->xvg->arena, _nvg_arena);
    }

    LINKED_ARENA_LEAK_DETECT_END(ted->undo_arena)

    return idx;
}

void ted_handle_mouse_down(TextEditor* ted, imgui_context* im)
{
    const imgui_pt pos       = im->pos_mouse_down;
    const uint32_t modifiers = im->frame.modifiers_mouse_down;

    ted_set_action(ted, TEXT_ACTION_MOVE);
    const int idx = ted_get_text_idx(ted, pos.x);
    if (idx != ted->ibeam_idx && ted->selection_start == ted->selection_end)
    {
        im->left_click_counter = 1;
    }
    bool selecting = modifiers == (PW_MOD_LEFT_BUTTON | PW_MOD_KEY_SHIFT);
    ted_set_ibeam_idx(ted, idx, selecting);

    if (im->left_click_counter == 2)
    {
        ted_set_action(ted, TEXT_ACTION_MOVE);
        int total_chars = xarr_len(ted->codepoints);
        if (total_chars)
        {
            // Find start and end word boundary
            int start = idx, end = idx;

            if (end < total_chars)
            {
                enum TextEditorCharType type = ted_get_char_type(ted->codepoints[end]);
                end++;
                while (end < total_chars && type == ted_get_char_type(ted->codepoints[end]))
                    end++;

#ifdef _WIN32
                while (end < total_chars && isspace_utf16(ted->codepoints[end]))
                    end++;
#endif
            }

            if (start > 0)
            {
                if (start >= total_chars)
                    start = total_chars - 1;
                enum TextEditorCharType type = ted_get_char_type(ted->codepoints[start]);
                while (start > 0 && type == ted_get_char_type(ted->codepoints[start - 1]))
                    start--;
            }

            ted->ibeam_idx       = end;
            ted->selection_start = start;
            ted->selection_end   = end;
            ted_handle_ibeam_moved(ted);
        }
    }
    else if (im->left_click_counter == 3)
    {
        ted_set_action(ted, TEXT_ACTION_MOVE);
        ted->ibeam_idx       = xarr_len(ted->codepoints);
        ted->selection_start = 0;
        ted->selection_end   = ted->ibeam_idx;
        ted_handle_ibeam_moved(ted);
    }
}

void ted_handle_mouse_drag(TextEditor* ted, imgui_context* im)
{
    float drag_x = im->pos_mouse_move.x;
    ted_set_action(ted, TEXT_ACTION_MOVE);
    int idx = ted_get_text_idx(ted, drag_x);
    ted_set_ibeam_idx(ted, idx, true);
}

void ted_init(TextEditor* ted, XVGCommandList* xvg)
{
    ted->xvg = xvg;
    xarr_setcap(ted->codepoints, 64);
    ted->undo_arena = linked_arena_create(1024 * 4 - sizeof(LinkedArena));
    ted_push_state(ted);
}

void ted_deinit(TextEditor* ted)
{
    xarr_free(ted->codepoints);
    linked_arena_destroy(ted->undo_arena);
}

void ted_set_text(TextEditor* ted, const char* text)
{
    size_t len = 0;
    int    cp  = 0;
    size_t cap = strlen(text) + 1;
    xarr_setcap(ted->codepoints, cap);

    while (*text != 0)
    {
        text                 = utf8codepoint(text, &cp);
        ted->codepoints[len] = cp;
        len++;
    }
    xarr_header(ted->codepoints)->length = len;
}

size_t ted_get_text(TextEditor* ted, char* buf, size_t buflen)
{
    const int* it  = ted->codepoints;
    const int* end = it + xarr_len(ted->codepoints);

    return ted_get_text_range(ted, buf, buflen, it, end);
}

void ted_clear(TextEditor* ted)
{
    linked_arena_clear(ted->undo_arena);
    ted->ref_current_state = NULL;
    xarr_setlen(ted->codepoints, 0);
    ted->ibeam_idx       = 0;
    ted->selection_start = 0;
    ted->selection_end   = 0;
    ted->text_offset     = 0;
    ted->last_action     = TEXT_ACTION_MOVE;
    ted_push_state(ted);
}

void ted_draw(TextEditor* ted, uint64_t frame_time_ns, const char* placeholder, bool has_keyboard_focus)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(ted->undo_arena)

    XVGCommandList* xvg = ted->xvg;

    xassert(ted->ibeam_idx >= 0);
    xassert(ted->selection_start >= 0);
    xassert(ted->selection_end >= 0);
    xassert(ted->selection_start <= ted->selection_end);

    const xvec4f* d = &ted->dimensions;

    float padding = ted->theme.padding_vertical;

    // xvg_command_set_scissor(xvg, d->x, d->y - padding, d->width, d->height + 2 * padding, 0);

    size_t          total_chars = xarr_len(ted->codepoints);
    const char*     text        = NULL;
    XVGGlyphLayout* glyphs      = NULL;
    size_t          glyphs_len  = 0;
    size_t          text_len    = 0;
    float           text_width  = 0;

    xassert(ted->ibeam_idx <= total_chars);
    xassert(ted->selection_start <= total_chars);
    xassert(ted->selection_end <= total_chars);

    XVGAlign alignment = ted->theme.is_centre_aligned ? XVG_ALIGN_CC : XVG_ALIGN_CL;

    if (total_chars)
    {
        size_t alloc_size = 4 * total_chars + 1;

        char* alloc_text = linked_arena_alloc(ted->undo_arena, alloc_size);

        text_len = ted_get_text(ted, alloc_text, alloc_size);
        text     = alloc_text;
        xassert(text_len == strlen(alloc_text));

        const XVGTextLayout* layout =
            xvg_create_text_layout(ted->xvg, alloc_text, alloc_text + text_len, ted->theme.font_size, 0, 1);
        glyphs     = xvg_layout_get_glyphs(layout);
        glyphs_len = layout->num_glyphs;

        text_width = (float)layout->xmax / xvg->xvg->backingScaleFactor;

        // Draw selection BG
        if (ted->selection_start != ted->selection_end)
        {
            LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(ted->xvg->arena, _nvg_arena);
            xassert(ted->selection_start < ted->selection_end);
            const float start_x = glyphs[ted->selection_start].x;
            const float end_x   = (glyphs[ted->selection_end - 1].x + glyphs[ted->selection_end - 1].rect.w);

            float selection_x     = start_x;
            float selection_width = end_x - start_x;

            selection_x     /= xvg->xvg->backingScaleFactor;
            selection_width /= xvg->xvg->backingScaleFactor;

            selection_x += d->x + ted->text_offset + ted->theme.padding_left;

            if (ted->theme.is_centre_aligned)
            {
                selection_x += d->width * 0.5f;
                selection_x -= text_width * 0.5f;
            }

            xvg_draw_solid_rectangle(
                xvg,
                selection_x,
                d->y + padding,
                selection_width,
                d->height - 2 * padding,
                ted->theme.col_selection_bg);
        }

        float x  = ted->theme.is_centre_aligned ? (d->x + d->width * 0.5)
                                                : (d->x + ted->text_offset + ted->theme.padding_left);
        float cy = d->y + d->height * 0.5;

        if (glyphs_len)
        {
            unsigned col = has_keyboard_focus ? ted->theme.col_text_active : ted->theme.col_text_inactive;
            xvg_draw_text_layout(xvg, layout, x, cy, alignment, col);
        }
    }

    // Draw ibeam
    if (has_keyboard_focus)
    {
        uint64_t time_then = ted->time_last_ibeam_move;
        uint64_t time_now  = frame_time_ns;
        // Show every half second
        uint64_t num_500ms_intervals = (time_now - time_then) / 500000000;

        if (num_500ms_intervals == 0 || (num_500ms_intervals & 1))
        {
            float ibeam_x = 0;
            if (glyphs_len)
            {
                int idx = ted->ibeam_idx;
                if (idx >= glyphs_len)
                    ibeam_x = text_width;
                else
                    ibeam_x = (float)glyphs[idx].x / xvg->xvg->backingScaleFactor;
                if (ibeam_x < 0)
                    ibeam_x = 0;
                ibeam_x += ted->text_offset;
            }
            // Centre alignment
            if (ted->theme.is_centre_aligned)
            {
                ibeam_x += d->width * 0.5f;
                ibeam_x -= text_width * 0.5f;
            }
            else
            {
                ibeam_x += ted->theme.padding_left;
            }

            ibeam_x += d->x;
            ibeam_x  = ceilf(ibeam_x);

            xvg_draw_solid_rectangle(
                xvg,
                ibeam_x - 1,
                d->y + padding,
                2,
                d->height - 2 * padding,
                ted->theme.col_ibeam);
        }
    }

    if (placeholder && has_keyboard_focus == false && total_chars == 0)
    {
        float x  = ted->theme.is_centre_aligned ? (d->x + d->width * 0.5) : (d->x + ted->theme.padding_left);
        float cy = d->y + d->height * 0.5;
        xvg_draw_text(xvg, x, cy, placeholder, 0, ted->theme.font_size, alignment, ted->theme.col_text_placeholder);
    }

    // xvg_command_set_scissor(xvg, 0, 0, gui_width, gui_height, 0);

    if (text)
        linked_arena_release(ted->undo_arena, text);

    LINKED_ARENA_LEAK_DETECT_END(ted->undo_arena)
}

void ted_activate(TextEditor* ted, const TextEditorTheme* theme, xvec4f dimensions, const xvec2f* pos, const char* text)
{
    ted->dimensions = dimensions;
    ted->theme      = *theme;

    if (text)
        ted_set_text(ted, text);

    ted->text_offset     = 0;
    ted->selection_start = 0;
    ted->selection_end   = 0;

    if (pos != NULL)
    {
        ted->ibeam_idx = ted_get_text_idx(ted, pos->x);
    }
    else
    {
        ted->ibeam_idx = xarr_len(ted->codepoints);
    }

    if (text)
    {
        ted->ref_current_state = NULL;
        linked_arena_clear(ted->undo_arena);
        ted->last_action = TEXT_ACTION_WRITE;
        ted_push_state(ted);
    }
}

void ted_deactivate(TextEditor* ted)
{
    ted->text_offset     = 0;
    ted->ibeam_idx       = 0;
    ted->selection_start = 0;
    ted->selection_end   = 0;
}
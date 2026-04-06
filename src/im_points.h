#ifndef IM_POINTS_H
#define IM_POINTS_H

#include <xhl/thread.h>
#include <xhl/vector.h>
#include <xhl/array2.h>

#include <imgui.h>

enum
{
    IMP_DEFAULT_SKEW_DRAG_RANGE           = 250,
    IMP_DEFAULT_POINT_DRAG_ERASE_DISTANCE = 24,
};

typedef enum IMPShapeType
{
    IMP_SHAPE_POINT,
    IMP_SHAPE_FLAT,
    IMP_SHAPE_LINEAR_ASC,
    IMP_SHAPE_LINEAR_DESC,
    IMP_SHAPE_CONCAVE_ASC,
    IMP_SHAPE_CONCAVE_DESC,
    IMP_SHAPE_CONVEX_ASC,
    IMP_SHAPE_CONVEX_DESC,
    IMP_SHAPE_COSINE_ASC,
    IMP_SHAPE_COSINE_DESC,
    IMP_SHAPE_TRIANGLE_UP,
    IMP_SHAPE_TRIANGLE_DOWN,
    IMP_SHAPE_COUNT,
} IMPShapeType;

typedef struct IMPointsLineCacheStraight
{
    float x1, y1, x2, y2;
} IMPointsLineCacheStraight;
typedef struct IMPointsLineCachePlot
{
    float    x, y, w, h;
    unsigned begin_idx, end_idx;
} IMPointsLineCachePlot;

typedef struct IMPointsLineCache
{
    unsigned                   num_lines;
    unsigned                   num_plots;
    IMPointsLineCacheStraight* lines;
    IMPointsLineCachePlot*     plots;
    size_t                     num_y_values;
    float                      y_values[];
} IMPointsLineCache;

typedef struct IMPointsData
{
    imgui_rect area;
    xvec2f     area_last_click_pos;

    // If false, should copy over the points array from the audio thread to the main thread
    bool main_points_valid;
    // Used to queue changes made to GUIs points before sending to the audio thread
    // Coordinates are normalised 0-1
    xvec3f* main_points;

    // Draggable points (widgets)
    // Coordinates are in window space
    bool     points_valid; // Set to false to copy main_points > points
    unsigned uid_points;
    xvec2f*  points;

    unsigned uid_skew_points;
    xvec2f*  skew_points;
    // Used as backup while doing non-destructive preview editing of points/skew_points
    xvec2f* points_copy;
    xvec2f* skew_points_copy;

    // Point multiselect
    xvec2f selection_start;
    xvec2f selection_end;
    int*   selected_point_indexes;      // indexes into points_copy
    int*   selected_point_indexes_copy; // backup of selection at beginning of selection drag
    // Used for hacks to make the current selection & hover work properly when previewing edits to points with the
    // drag-auto-erase feature
    int selected_point_idx;

    unsigned uid_grid;

    size_t             line_cache_cap_bytes;
    IMPointsLineCache* line_cache;

    struct
    {
        uint32_t col_line;
        float    line_stroke_width;
        float    point_click_radius; // recommended: 12px
        float    point_radius;       // recommended: 4px
        float    skew_point_radius;  // recommended: 3px

        uint32_t col_point_hover_bg;

        uint32_t col_skewpoint_inner;
        uint32_t col_skewpoint_outer;
        float    skewpoint_stroke_width; // recommended: 1.5px

        uint32_t col_point;
        uint32_t col_point_selected;
        uint32_t col_selection_box;
    } theme;
} IMPointsData;

typedef struct IMPointsFrameContext
{
    IMPointsData*          imp;   // not owned
    struct XVGCommandList* xcl;   // not owned
    struct imgui_context*  im;    // not owned
    struct LinkedArena*    arena; // not owned
    void*                  pw;    // not owned

    // Recreate the cache
    bool should_update_cached_path;
    // If true, updates the main points
    bool should_update_main_points_with_points;
    bool should_update_audio_points_with_main_points;

    bool did_reload;
    int  pt_hover_idx;
    int  pt_hover_skew_idx;
    int  delete_pt_idx;
} IMPointsFrameContext;

static IMPointsFrameContext imp_frame_context_new(
    IMPointsData*          imp,
    struct XVGCommandList* xcl,
    struct imgui_context*  im,
    struct LinkedArena*    arena,
    void*                  pw)
{
    IMPointsFrameContext framestate = {0};
    framestate.imp                  = imp;
    framestate.xcl                  = xcl;
    framestate.im                   = im;
    framestate.arena                = arena;
    framestate.pw                   = pw;

    framestate.pt_hover_idx      = -1;
    framestate.pt_hover_skew_idx = -1;
    framestate.delete_pt_idx     = -1;
    return framestate;
}

static void imp_reload(IMPointsData* imp)
{
    imp->main_points_valid = false;
    imp->points_valid      = false;
}

static void imp_copy(IMPointsData* a, IMPointsData* b)
{
    xarr_copy(a->points, b->points);
    xarr_copy(a->skew_points, b->skew_points);
    xarr_copy(a->points_copy, b->points_copy);
    xarr_copy(a->skew_points_copy, b->skew_points_copy);

    imp_reload(b);
}

static void imp_init(IMPointsData* imp, unsigned uid_points, unsigned uid_skew_points, unsigned uid_grid)
{
    imp->uid_points      = uid_points;
    imp->uid_skew_points = uid_skew_points;
    imp->uid_grid        = uid_grid;
}

void imp_deinit(IMPointsData*);

// Returns true when p_audio_points is modified
// Handles all mouse events, caching, and sychronisation with audio thread data
void imp_run(
    IMPointsFrameContext* fstate,
    imgui_rect            area,
    int                   num_grid_x,
    int                   num_grid_y,
    IMPShapeType          current_shape,

    xvec3f**       p_audio_points,
    xt_spinlock_t* p_lock // optional
);

void imp_draw(IMPointsFrameContext* fstate);

void imp_render_y_values(const IMPointsData*, float* buffer, size_t bufferlen, float y_range_min, float y_range_max);
void imp_sync_main_points(IMPointsData* imp, const xvec3f* ap);

#endif // IM_POINTS_H

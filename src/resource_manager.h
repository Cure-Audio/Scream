#pragma once
/*
Quick and dirty attempt at adding "automatic garbage collection" or "immediate mode state/resources" to the GUI
Helps to solve retained mode problems like:
- Automatic resizing of framebuffers
- Detects and destroys framebuffers not in use. Useful for tabbed and modal content using framebuffers
- State used by deprecated code/procedures in your program is never allocated, and you never need to remember to
  remove that state from structs later.
Inspired by ideas from this video: https://www.youtube.com/watch?v=-cWJRZhALD
NOTE: this does not implement every idea in the video
How it works:
- User calls resource_get_xxx
    - if it doesnt exist, its allocated and initialised
    - if it already exists, its loaded from cache
    - resourced is flagged "used"
- At the end of every frame, all resources without the "used" flag are destroyed. Memeory is copied from one pool to
  another in a flipping A>B B>A system. Resources are uninitialised and dropped and memory is defragged in one pass
- When your app shuts down, all resources are destroyed. This means you never ever have to manually manage memory
- Additional persistance flags exist for objects with more complicated lifetimes that need to persist multiple frames
  when not in use
- You can manually find or destroy resources if you need using resource_find() & _resource_destroy(), although at that
  point it may be worth wondering whether a retained mode solution is better.

IDEA:   This same system, or something like could be used for clever automatic compositing
        Consider that the wip_libs/imgui.h lib requires that widgets with a higher z-order request their events first.
        This forces structure on the developer, to write their code for the topmost widgets first.
        Rendering on the other hand is done back to front.
        This intriduces a problem for modal "views" (a popup window with its own background colour and foreground
        widgets).
        One way to tackle that problem may be to render all these modal views to a separate framebuffer, and composite
        them later. If we can depend on this strict order, and all framebuffers are stored in this resource manager, we
        could safely loop backwards through all "used" framebuffers and handle our compositing that way.
        This will require a rigid structure on behalf of the developer, but it's possible that its always fine in
        practice.
        One criticism of the above idea is that say a clicked button opens up a popup menu modal. The button sets a
        flag, but presumably the code for rendering that modal lives at the top:

        // tick()
        if (gui->modal_open)
        {
            // ... draw contents
        }
        ....
        if (do_button(width, height))
        {
            gui->modal_open = true;
        }
        ...
        // do auto compositing

        The above code will cause the modal to render on the next frame, rather than the one being rendered.
        When code is written quickly and the author doesn't care that much about polish, that may be fine, but if every
        other action in the GUI happens that frame without any additional frame latency, this delay stands out a little,
        even if it's still crazy fast in the grand scheme of things (eg. VSCode, the code editor I'm writing this with
        has an input -> render latency of 33ms last time I checked, which is probably 1 frame (16.6ms) longer than it
        should be).

        A solution to that may be to wrap `gui->modal_open = true;` in a fucntion that will set that `modal_open` flag,
        and then render the contents of the modal to a framebuffer which is created with a special flag. The compositor
        reads flag and makes sure its rendered last, and clears the flag when its done.
        Presumably new framebuffers that are created this way will ALWAYS be expected to have the highest z-order at the
        time

TODO: Build up a separate array of pointers to items in the order they are accessed. Use this when defragging memory in
      order to ensure data access is contiguous in future frames.
*/

#include <xhl/debug.h>
#include <xvg.h>

typedef enum ResourceType
{
    // sg_shader
    RESOURCE_TYPE_SHADER,
    // sg_pipeline
    RESOURCE_TYPE_PIPELINE,
    // SGNVGframebuffer
    // RESOURCE_TYPE_FRAMEBUFFER, // TODO: replace
    // void*, size_t
    RESOURCE_TYPE_DATA_FIXED,
    // RESOURCE_TYPE_DATA_DYNAMIC,
    // sg_buffer + sg_view
    // RESOURCE_TYPE_STORAGEBUFFER,
    // Nanovg have their own 'NVGtexture' type, but I may want to replace it with this
    // sg_image + sg_view + width _ height
    // RESOURCE_TYPE_TEXTURE,
} ResourceType;

enum
{
    RESOURCE_FLAG_USED_FRAME         = 1 << 0,
    RESOURCE_FLAG_DESTROYED          = 1 << 1, //
    RESOURCE_FLAG_NODESTROY_ENDFRAME = 1 << 2, // Never destroy resource at end of frame
};

typedef union ResourceID
{
    uint64_t u64;
    void*    ptr;
    struct
    {
        unsigned u32_1;
        unsigned u32_2;
    };
} ResourceID;

typedef struct ResourceHeader
{
    ResourceID   id;
    ResourceType type;
    uint32_t     flags;
    size_t       size;

    unsigned char payload[];
} ResourceHeader;

typedef struct ResourceList
{
    uint32_t num_new_items;
    uint32_t num_accessed_items;

    // Last accessed index + 1
    uint32_t predict_idx;

    uint32_t         num_items;
    uint32_t         cap_items;
    ResourceHeader** items;

    LinkedArena* arena;
} ResourceList;

typedef struct ResourceManager
{
    ResourceList list_a;
    ResourceList list_b;

    ResourceList* list_current; // a or b
} ResourceManager;

void resources_init(ResourceManager* rm, size_t init_size);
void resources_deinit(ResourceManager* rm, XVG* xvg);
void resources_end_frame(ResourceManager* rm, XVG* xvg);

ResourceHeader* resource_find(ResourceManager* rm, ResourceID id);
void            _resource_destroy(ResourceHeader* res, XVG* xvg);
void            resources_destroy_type(ResourceManager* rm, XVG* xvg, ResourceType type);

typedef const sg_shader_desc* (*sokol_shdc_shader_t)(sg_backend backend);
bool resource_get_pipeline(ResourceManager* rm, sg_pipeline* pipelne, sokol_shdc_shader_t method, uint32_t flags);

// bool resource_get_framebuffer(
//     ResourceManager*  rm,
//     uint32_t          id,
//     SGNVGframebuffer* fb,
//     NVGcontext*       nvg,
//     unsigned          w,
//     unsigned          h,
//     uint32_t          flags);

bool resource_get_data_fixed(ResourceManager* rm, ResourceID id, void** data, size_t size, uint32_t flags);

static inline ResourceID resource_make_id_shader(sokol_shdc_shader_t method)
{
    ResourceID id  = {.ptr = method};
    id.u64        += RESOURCE_TYPE_SHADER;
    return id;
}
static inline ResourceID resource_make_id_pipeline(sokol_shdc_shader_t method)
{
    ResourceID id  = {.ptr = method};
    id.u64        += RESOURCE_TYPE_PIPELINE;
    return id;
}

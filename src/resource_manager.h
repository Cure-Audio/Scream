#pragma once
// Quick and dirty attempt at adding "automatic garbage collection" or "immediate mode assets/resources" to the GUI
// Resources may include
#include <nanovg2.h>
#include <xhl/debug.h>

typedef enum ResourceType
{
    // sg_shader
    RESOURCE_TYPE_SHADER,
    // sg_pipeline
    RESOURCE_TYPE_PIPELINE,
    // SGNVGframebuffer
    RESOURCE_TYPE_FRAMEBUFFER,
    // sg_buffer + sg_view
    RESOURCE_TYPE_STORAGEBUFFER,
    // Nanovg have their own 'NVGtexture' type, but I may want to replace it with this
    // sg_image + sg_view + width _ height
    RESOURCE_TYPE_TEXTURE,
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
void resources_deinit(ResourceManager* rm, NVGcontext* nvg);
void resources_end_frame(ResourceManager* rm, NVGcontext* nvg);

typedef const sg_shader_desc* (*sokol_shdc_shader_t)(sg_backend backend);
bool resource_get_pipeline(ResourceManager* rm, sg_pipeline* pipelne, sokol_shdc_shader_t method, uint32_t flags);

bool resource_get_framebuffer(
    ResourceManager*  rm,
    uint32_t          id,
    SGNVGframebuffer* fb,
    NVGcontext*       nvg,
    unsigned          w,
    unsigned          h,
    uint32_t          flags);

bool resource_get_storagebuffer(uint32_t flags);
bool resource_get_texture(uint32_t flags);
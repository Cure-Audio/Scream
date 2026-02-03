#include "resource_manager.h"
#include <string.h>

ResourceHeader* resource_find(ResourceManager* rm, ResourceID id)
{
    ResourceList* list = rm->list_current;
    xassert(list);

    ResourceHeader* head = NULL;
    unsigned        idx  = list->predict_idx;
    for (unsigned i = 0; i < list->num_items; i++, idx++)
    {
        if (idx >= list->num_items)
            idx -= list->num_items;
        xassert(idx >= 0 && idx < list->num_items);
        ResourceHeader* res       = list->items[idx];
        ResourceHeader(*view)[32] = (void*)list->items;
        if (res->id.u64 == id.u64)
        {
            head = res;
            if ((head->flags & RESOURCE_FLAG_USED_FRAME) == 0)
                list->num_accessed_items++;
            head->flags |= RESOURCE_FLAG_USED_FRAME;
            break;
        }
    }
    if (head)
        list->predict_idx = idx + 1;
    return head;
}

ResourceHeader* resource_create(ResourceManager* rm, size_t num_bytes, ResourceID id, ResourceType type, uint32_t flags)
{
    ResourceHeader* res;

    size_t size  = (num_bytes + 7) & ~7;
    size        += sizeof(*res);

    ResourceList* list = rm->list_current;
    xassert(list);

#ifndef NDEBUG
    // Make sure resource with ID doesn't alredy exist
    for (int i = 0; i < list->num_items; i++)
    {
        ResourceHeader* temp = list->items[i];
        xassert(id.u64 != temp->id.u64);
    }
#endif

    res = linked_arena_alloc_clear(list->arena, size);

    // Add item to array
    if (list->num_items + 1 > list->cap_items)
    {
        list->cap_items *= 2;
        if (list->cap_items < 32)
            list->cap_items = 32;
        ResourceHeader** next_arr = linked_arena_alloc(list->arena, sizeof(void*) * list->cap_items);
        memcpy(next_arr, list->items, sizeof(void*) * list->num_items);
        list->items = next_arr;
    }
    list->items[list->num_items++] = res;
    list->num_new_items++;

    res->id    = id;
    res->type  = type;
    res->flags = flags | RESOURCE_FLAG_USED_FRAME;
    res->size  = size;

    return res;
}

bool resource_get_shader(ResourceManager* rm, sg_shader* shader, sokol_shdc_shader_t method, uint32_t flags)
{
    ResourceID id = resource_make_id_shader(method);

    ResourceHeader* res = resource_find(rm, id);
    if (res == NULL)
    {
        // println("Create shader");
        res = resource_create(rm, sizeof(*shader), id, RESOURCE_TYPE_SHADER, flags);

        sg_shader* sh = (void*)&res->payload;
        *sh           = sg_make_shader(method(sg_query_backend()));
    }

    xassert(res->type == RESOURCE_TYPE_SHADER);
    *shader = *(sg_shader*)&res->payload;

    return res != NULL;
}

bool resource_get_pipeline(ResourceManager* rm, sg_pipeline* pipelne, sokol_shdc_shader_t method, uint32_t flags)
{
    ResourceID      id  = resource_make_id_pipeline(method);
    ResourceHeader* res = resource_find(rm, id);
    if (res == NULL)
    {
        sg_shader shd;
        if (resource_get_shader(rm, &shd, method, flags | RESOURCE_FLAG_NODESTROY_ENDFRAME))
        {
            // println("Create pipeline");
            res = resource_create(rm, sizeof(*pipelne), id, RESOURCE_TYPE_PIPELINE, flags);

            sg_pipeline* pip = (void*)&res->payload;
            *pip             = sg_make_pipeline(&(sg_pipeline_desc){
                            .shader    = shd,
                            .colors[0] = {
                                .write_mask = SG_COLORMASK_RGBA,
                                .blend      = {
                                         .enabled          = true,
                                         .src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA,
                                         .src_factor_alpha = SG_BLENDFACTOR_ONE,
                                         .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                         .dst_factor_alpha = SG_BLENDFACTOR_ONE,
                    }}});
        }
    }

    xassert(res->type == RESOURCE_TYPE_PIPELINE);
    *pipelne = *(sg_pipeline*)&res->payload;

    return res != NULL;
}

// bool resource_get_framebuffer(
//     ResourceManager*  rm,
//     uint32_t          id,
//     SGNVGframebuffer* fb,
//     XVG*       xvg,
//     unsigned          w,
//     unsigned          h,
//     uint32_t          flags)
// {
//     xassert(w <= 0xffff);
//     xassert(h <= 0xffff);
//     ResourceID      res_id = {.u32_1 = id, .u32_2 = w | (h << 16)};
//     ResourceHeader* res    = resource_find(rm, res_id);
//     if (res == NULL)
//     {
//         res = resource_create(rm, sizeof(*fb), res_id, RESOURCE_TYPE_FRAMEBUFFER, flags);

//         SGNVGframebuffer* res_fb  = (void*)&res->payload;
//         *res_fb                   = snvgCreateFramebuffer(xvg, w, h);
//         res_fb                   += 0;
//     }
//     xassert(res->type == RESOURCE_TYPE_FRAMEBUFFER);
//     *fb = *(SGNVGframebuffer*)&res->payload;

//     return res != NULL;
// }

bool resource_get_data_fixed(ResourceManager* rm, ResourceID id, void** data, size_t size, uint32_t flags)
{
    ResourceHeader* res = resource_find(rm, id);
    if (res == NULL)
    {
        // println("Create Data (fixed)");
        res = resource_create(rm, size, id, RESOURCE_TYPE_DATA_FIXED, flags);
    }

    xassert(res->type == RESOURCE_TYPE_DATA_FIXED);
    *data = &res->payload;

    return res != NULL;
}

// TODO
// bool resource_get_texture();
// bool resource_get_storagebuffer();

void _resource_destroy(ResourceHeader* res, XVG* xvg)
{
    if (res->flags & RESOURCE_FLAG_DESTROYED)
        return;
    void* data = &res->payload;
    switch (res->type)
    {
    case RESOURCE_TYPE_SHADER:
    {
        // println("Destroy RESOURCE_TYPE_SHADER");
        sg_shader* sh = data;
        sg_destroy_shader(*sh);
        break;
    }
    case RESOURCE_TYPE_PIPELINE:
    {
        // println("Destroy RESOURCE_TYPE_PIPELINE");
        sg_pipeline* pip = data;
        sg_destroy_pipeline(*pip);
        break;
    }
    // case RESOURCE_TYPE_FRAMEBUFFER:
    // {
    //     // println("Destroy RESOURCE_TYPE_FRAMEBUFFER");
    //     SGNVGframebuffer* fb = data;
    //     snvgDestroyFramebuffer(xvg, fb);
    //     break;
    // }
    case RESOURCE_TYPE_DATA_FIXED:
        break;
        // case RESOURCE_TYPE_STORAGEBUFFER:
        //     xassert(false); // TODO
        //     break;
        // case RESOURCE_TYPE_TEXTURE:
        //     xassert(false); // TODO
        //     break;
    }
    res->flags |= RESOURCE_FLAG_DESTROYED;
}

void resources_destroy_type(ResourceManager* rm, XVG* xvg, ResourceType type)
{
    ResourceList* list = rm->list_current;
    xassert(list);
    for (int i = 0; i < list->num_items; i++)
    {
        ResourceHeader* res = list->items[i];
        if (res->type == type)
            _resource_destroy(res, xvg);
    }
}

// Destroy unused resources & defrag memory
void resources_end_frame(ResourceManager* rm, XVG* xvg)
{
    ResourceList* src = rm->list_current;
    ResourceList* dst = rm->list_current == &rm->list_a ? &rm->list_b : &rm->list_a;

    // no meaningful changes to resources
    if (src->num_accessed_items == src->num_items && src->num_new_items == 0)
        return;

    linked_arena_clear(dst->arena);
    _Static_assert(offsetof(ResourceList, arena) == sizeof(ResourceList) - sizeof(void*));
    memset(dst, 0, sizeof(ResourceList) - sizeof(void*)); // clear all but arena ptr

    dst->cap_items = src->num_items * 2;
    dst->items     = linked_arena_alloc(dst->arena, dst->cap_items * sizeof(*dst->items));

    // Defrag array and destroy unused resources in one pass
    for (unsigned i = 0; i < src->num_items; i++)
    {
        ResourceHeader* src_res = src->items[i];
        if (src_res->flags == 0) // unsused resource. destroy
        {
            _resource_destroy(src_res, xvg);
        }
        else
        {
            xassert(src_res->flags & (RESOURCE_FLAG_USED_FRAME | RESOURCE_FLAG_NODESTROY_ENDFRAME));
            ResourceHeader* dst_res = linked_arena_alloc(dst->arena, src_res->size);
            memcpy(dst_res, src_res, src_res->size);
            dst->items[dst->num_items++] = dst_res;

            dst_res->flags &= ~RESOURCE_FLAG_USED_FRAME;
        }
    }

    // Flip current list for next frame
    rm->list_current = dst;
}

void resources_init(ResourceManager* rm, size_t init_size)
{
    rm->list_a.arena = linked_arena_create_ex(0, init_size);
    void* hint       = linked_arena_make_hint(rm->list_a.arena);
    rm->list_b.arena = linked_arena_create_ex(hint, init_size);
    rm->list_current = &rm->list_a;
}

void resources_deinit(ResourceManager* rm, XVG* xvg)
{
    ResourceList* list = rm->list_current;
    xassert(list);
    for (int i = 0; i < list->num_items; i++)
    {
        ResourceHeader* res = list->items[i];
        _resource_destroy(res, xvg);
    }
    linked_arena_destroy(rm->list_b.arena);
    linked_arena_destroy(rm->list_a.arena);
}
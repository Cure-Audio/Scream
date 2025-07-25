#pragma once
#include <stddef.h>
#include <stdint.h>
#include <xhl/debug.h>

typedef struct LinkedArena
{
    size_t capacity;
    size_t size;

    size_t _padding;

    struct LinkedArena* next;
} LinkedArena;

LinkedArena* linked_arena_create(size_t init_cap);
void         linked_arena_destroy(LinkedArena* arena);
void*        linked_arena_alloc(LinkedArena* arena, size_t size);
void* linked_arena_alloc_clear(LinkedArena* arena, size_t size); // Additionally zeros returned memory for convenience
void  linked_arena_release(LinkedArena* arena, const void* const ptr);
void  linked_arena_clear(LinkedArena* arena);
void  linked_arena_prune(LinkedArena* arena); // Destroy unused arenas. Won't destroy first item

#ifdef NDEBUG
#define LINKED_ARENA_LEAK_DETECT_BEGIN(arena)
#define LINKED_ARENA_LEAK_DETECT_END(arena)
#else
#define LINKED_ARENA_LEAK_DETECT_BEGIN(arena) size_t _arena_size = arena->size;
#define LINKED_ARENA_LEAK_DETECT_END(arena)   xassert(_arena_size == arena->size);
#endif
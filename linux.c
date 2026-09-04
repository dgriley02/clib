#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "base.h"

static size_t __MemAlignForward(size_t p, size_t alignment) {
    return (p + alignment - 1) & ~(alignment - 1);
}

static void __MemZero(void* p, size_t n) {
    memset(p, 0, n);
}

static u32 __GetPageSize(void) {
    static u32 cached_page_size = 0;
    if (cached_page_size == 0) {
        cached_page_size = (u32)sysconf(_SC_PAGESIZE);
    }
    return cached_page_size;
}

static void* __VMReserve(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

#define ARENA_DEFAULT_RESERVE MegaBytes(64)

struct Arena {
    u64 reserved, used;
    void* base;
};

void* _ArenaPush(Arena* a, u64 size, u64 alignment, b32 clearToZero) {
    if (!a->base) {
        a->base = __VMReserve(ARENA_DEFAULT_RESERVE);
        a->reserved = ARENA_DEFAULT_RESERVE;
        if (!a->base) return NULL;
    }
    u64 start = __MemAlignForward(a->used, alignment);
    u64 end = start + size;
    if (end > a->reserved) {
        return NULL; // TODO: maybe make this crash somehow platform agnostic...
    }
    a->used = start + size;
    if (clearToZero) __MemZero((u8*)a->base + start, size);
    return (void*)((u8*)a->base + start);
}

Arena* ArenaPushArena(Arena* arena, u64 size) {
    Arena* new_arena = ArenaPush(arena, Arena);
    new_arena->used = 0;
    new_arena->reserved = size;
    new_arena->base = ArenaPushArray(new_arena, u8, size);
    return new_arena;
}

void ArenaReset(Arena* a, b32 decommit) {
    a->used = 0;
}

static Arena g_arena_app = {0};
static __thread Arena g_scratch = {0};

Arena* ArenaInit(u64 appMemory) {
    if (appMemory == 0) appMemory = ARENA_DEFAULT_RESERVE;
    g_arena_app.base = __VMReserve(appMemory);
    g_arena_app.reserved = appMemory;
    if (!g_arena_app.base) return NULL;
    return &g_arena_app;
}

Arena* ArenaScratchInit(u64 size) {
    if (!g_scratch.base) {
        g_scratch.base = __VMReserve(size);
        g_scratch.reserved = size;
        if (!g_scratch.base) return NULL;
    }
    return &g_scratch;
}

Arena* ArenaScratch() { return ArenaScratchInit(MegaBytes(4)); }
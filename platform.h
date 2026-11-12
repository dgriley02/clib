#ifndef CLIB_PLATFORM
#define CLIB_PLATFORM

void MemCopy(void* dst, void* src, size_t n);
i32  MemCmp(void* a, void* b, size_t n);
void MemZero(void* p, size_t n);

struct Arena;
void* _ArenaPush(struct Arena* a, u64 size, u64 alignment, b32 clearToZero);
struct Arena* ArenaPushArena(struct Arena* arena, u64 size);
void ArenaReset(struct Arena* a);
u64 ArenaMark(struct Arena* a);
void ArenaResetTo(struct Arena* a, u64 mark);

struct Arena* ArenaInit(u64 appMemory);
struct Arena* ArenaScratch();
struct Arena* ArenaScratchInit(u64 size);

#define ArenaPush(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), false))
#define ArenaPushZeroed(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), true))
#define ArenaPushArray(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), false))
#define ArenaPushArrayZeroed(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), true))

struct File;
enum FileOpenMode { 
    FILE_OPEN_MODE_READ  = 1 << 0, 
    FILE_OPEN_MODE_WRITE = 1 << 1
};
b32 FileOpen(struct Arena* arena, String path, enum FileOpenMode mode, struct File* out_file);
b32 FileRead(struct Arena* arena, struct File* file, u8** out_data, u64* out_n);
b32 FileWrite(struct File* file, u8* data, int n);
b32 FileClose(struct File* file);

#ifdef CLIB_PLATFORM_IMPL

#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

void MemCopy(void* dst, void* src, size_t n) { memcpy(dst, src, n); }
i32  MemCmp(void* a, void* b, size_t n) { return (i32)memcmp(a, b, n); }
void MemZero(void* p, size_t n) { memset(p, 0, n); }

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

void* _ArenaPush(struct Arena* a, u64 size, u64 alignment, b32 clearToZero) {
    if (!a->base) {
        a->base = __VMReserve(ARENA_DEFAULT_RESERVE);
        a->reserved = ARENA_DEFAULT_RESERVE;
        if (!a->base) return NULL;
    }
    u64 start = ALIGN_UP(a->used, alignment);
    u64 end = start + size;
    if (end > a->reserved) {
        return NULL; // TODO: maybe make this crash somehow platform agnostic...
    }
    a->used = start + size;
    if (clearToZero) MemZero((u8*)a->base + start, size);
    return (void*)((u8*)a->base + start);
}

struct Arena* ArenaPushArena(struct Arena* arena, u64 size) {
    struct Arena* new_arena = ArenaPush(arena, struct Arena);
    new_arena->used = 0;
    new_arena->reserved = size;
    new_arena->base = ArenaPushArray(new_arena, u8, size);
    return new_arena;
}

void ArenaReset(struct Arena* a) { a->used = 0; }
u64 ArenaMark(struct Arena* a) { return a->used; }
void ArenaResetTo(struct Arena* a, u64 mark) { a->used = mark; }

static struct Arena g_arena_app = {0};
static __thread struct Arena g_scratch = {0};

struct Arena* ArenaInit(u64 appMemory) {
    if (appMemory == 0) appMemory = ARENA_DEFAULT_RESERVE;
    g_arena_app.base = __VMReserve(appMemory);
    g_arena_app.reserved = appMemory;
    if (!g_arena_app.base) return NULL;
    return &g_arena_app;
}

struct Arena* ArenaScratchInit(u64 size) {
    if (!g_scratch.base) {
        g_scratch.base = __VMReserve(size);
        g_scratch.reserved = size;
        if (!g_scratch.base) return NULL;
    }
    return &g_scratch;
}

struct Arena* ArenaScratch() { return ArenaScratchInit(MegaBytes(4)); }

// Files

struct File
{
    int fd;
    String name;
};

b32 FileOpen(struct Arena* arena, String path, enum FileOpenMode mode, struct File* out_file) {
    int flags;
    if (FlagExists(mode, FILE_OPEN_MODE_READ | FILE_OPEN_MODE_WRITE)) {
        flags = O_RDWR;
    } else if (FlagExists(mode, FILE_OPEN_MODE_READ)) {
        flags = O_RDONLY;
    } else if (FlagExists(mode, FILE_OPEN_MODE_WRITE)) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else { 
        return false; 
    }

    u64 mark = ArenaMark(arena);
    const char* path_cstr = StrToCStr(arena, path);
    int fd = open(path_cstr, flags, 0666);

    if (fd < 0) {
        LogError("Failed to open file '%s': %s (errno: %d)", path_cstr, strerror(errno), errno);
        ArenaResetTo(arena, mark);
        return false;   
    }
    ArenaResetTo(arena, mark);
    out_file->fd = fd;
    StrCopy(arena, path, &out_file->name);
    return true;
}

b32 FileRead(struct Arena* arena, struct File* file, u8** out_data, u64* out_n) {
    struct stat file_stat;
    if (fstat(file->fd, &file_stat) != 0) return false;
    u64 size = (u64)file_stat.st_size;

    u64 mark = ArenaMark(arena);
    u8* buf = ArenaPushArray(arena, u8, size);
    ssize_t r = read(file->fd, buf, size);
    if (r < 0 || (u64)r != size) {
        LogError("Failed to read file %s", StrToCStr(arena, file->name)); 
        ArenaResetTo(arena, mark);
        return false;   
    }
    *out_data = buf;
    *out_n = size;
    return true;
}

b32 FileWrite(struct File* file, u8* data, int n) {
    if (write(file->fd, data, n) != n) {
        LogError("Failed to write file %s", StrToCStr(ArenaScratch(), file->name));
        return false;
    }
    return true;
}

b32 FileClose(struct File* file) {
    if (close(file->fd) != 0) {
        LogError("Failed to close file %s", StrToCStr(ArenaScratch(), file->name));
        return false;
    }
    return true;
}

#elif TODO_PLATFORM_WINDOWS

#endif

#endif // CLIB_PLATFORM_IMPL
#endif // CLIB_PLATFORM

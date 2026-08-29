#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  b8;
typedef int16_t b16;
typedef int32_t b32;
typedef int64_t b64;

typedef float f32;
typedef double f64;


#define true  1
#define false 0

#define KiloBytes(n) (1024 * n##ull)
#define MegaBytes(n) (1024*1024 * n##ull)
#define GigaBytes(n) (1024*1024*1024 * n##ull)

#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Min(a, b) ((a) < (b) ? (a) : (b))

#define FlagExists(_enum, i) ((_enum) & (i) == (i))
#define FlagSet(_enum, i) ((_enum) |= (i))
#define FlagClear(_enum, i) ((_enum) &= ~(i))
#define FlagToggle(_enum, i) ((_enum) ^= (i))

#if defined(__linux)

#include <sys/mman.h>

#define ALIGN_OF(X) (__alignof__(X))

#define PAGE_SIZE KiloBytes(4)

static void* __VMReserve(size_t size) {
    void* p = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static b32 __VMCommit(void* p, size_t size) {
    return mprotect(p, size, PROT_READ | PROT_WRITE) == 0;
}
static void __VMDecommit(void* p, size_t size) {
    madvise(p, size, MADV_DONTNEED);
    mprotect(p, size, PROT_NONE);
}
static void __VMDereserve(void* p, size_t size) {
    munmap(p, size);
}

#endif

static size_t __MemAlignForward(size_t p, size_t alignment) {
    return (p + alignment - 1) & (-alignment);
}

static void __MemZero(void* p, size_t n) {
    memset(p, 0, n);
}

typedef struct {
    u64 reserved;
    u64 committed;
    u64 used;
    void* base;
} Arena;

#define ARENA_DEFAULT_RESERVE MegaBytes(64)
#define ARENA_COMMIT_GRANULARITY KiloBytes(64)

#define ArenaPush(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), false))
#define ArenaPushZeroed(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), true))
#define ArenaPushArray(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), false))
#define ArenaPushArrayZeroed(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), true))

void* _ArenaPush(Arena* a, u64 size, u64 alignment, b32 clearToZero) {
    if (!a->base) {
        a->base = __VMReserve(ARENA_DEFAULT_RESERVE);
        a->reserved = ARENA_DEFAULT_RESERVE;
        if (!a->base) return NULL;
    }
    u64 offset = __MemAlignForward(a->used, alignment);
    u64 end = offset + size;
    if (end > a->reserved) {
        return NULL; // TODO: maybe make this crash somehow platform agnostic...
    }
    if (end > a->committed) {
        u64 required = end - a->committed;
        u64 commit_size = Max(required, ARENA_COMMIT_GRANULARITY);
        commit_size = __MemAlignForward(commit_size, PAGE_SIZE);
        if (!__VMCommit((u8*)a->base + a->committed, commit_size)) {
            return NULL; // TODO: crash
        }
        a->committed += commit_size;
    }
    a->used = offset + size;
    if (clearToZero) __MemZero((u8*)a->base + offset, size);
    return a->base + offset;
}

void ArenaReset(Arena* a) {
    a->used = 0;
}

void ArenaDecommit(Arena* a) {
    if (a->base) {
        __VMDecommit(a->base, a->committed);
    }
    a->used = a->committed = 0;
}

// ----------- Strings ------------

typedef struct {
    u32 size; // in bytes
    const char* ptr;
} String;

typedef struct {
    union {
        struct {
            u32 used;
            char* buffer;
        };
        String value;
    };
    u64 capacity;
} StrBuilder;

#define S(s) (__StrMake(sizeof(s) - 1, s))

String __StrMake(u32 size, const char* chars) { 
    String s;
    s.size = size;
    s.ptr = chars;
    return s;
}

String FromCStr(const char * c_str) {
    return __StrMake(strlen(c_str), c_str);
}

char* StrToCStr(Arena* a, String s) {
    char* out = ArenaPushArray(a, char, s.size + 1);
    out[s.size] = '\0';
    return out;
}

String StrSlice(String s, u32 start, u32 end) {
    String out;
    out.size = end - start;
    out.ptr = s.ptr + start;
    return out;
}

typedef enum {
    STRFIND_FLAG_NONE,
    STRFIND_FLAG_SEARCH_BACKARDS
} STRFIND_FLAGS;

b32 StrFind(String s, char c, STRFIND_FLAGS flags, u32* index) {
    b32 forwards = !FlagExists(flags, STRFIND_FLAG_SEARCH_BACKARDS);
    u32 start = forwards ? 0 : s.size - 1;
    u32 end = forwards ? s.size : 0; 
    u32 i = start;
    while (i != end) {
        if (s.ptr[i] == c) { *index = i; return true;}
        i = forwards ? i + 1 : i - 1;
    }
    return false;
}

b32 StrEqualsCStr(String s, const char* c) {
    return memcmp(s.ptr, c, s.size) == 0;
}
b32 CStrEqualsStr(const char* c, String s) {
    return memcmp(c, s.ptr, s.size) == 0;
}
b32 StrEquals(String s1, String s2) {
    return memcmp(s1.ptr, s2.ptr, s1.size) == 0;
}

#define STR_BUILDER_DEFAULT_CAPACITY (128ull)

static void __StrBuilderEnsureLargeEnough(Arena* arena, StrBuilder* builder, u64 required) {
    if (builder->capacity <= 0) {
        builder->capacity = STR_BUILDER_DEFAULT_CAPACITY;
    }
    if (!builder->buffer) {
        builder->buffer = ArenaPushArray(arena, u8, builder->capacity);
    }
    u64 end = builder->used + required + 1; // leave space for null terminator if we want to export c_str
    if (end > builder->capacity) { 
        u64 new_cap = Max(builder->capacity * 2, end);
        char* old = builder->buffer;
        builder->buffer = ArenaPushArray(arena, u8, new_cap); 
        memcpy(builder->buffer, old, builder->used);
        builder->capacity = new_cap;
    }
}

void StrBuildStr(Arena* arena, StrBuilder* builder, String s) {
    __StrBuilderEnsureLargeEnough(arena, builder, s.size);
    memcpy(builder->buffer + builder->used, s.ptr, s.size);
    builder->used += s.size;
}

void StrBuildCStr(Arena* arena, StrBuilder* builder, const char* c_str) {
    StrBuildStr(arena, builder, FromCStr(c_str));
}

void StrBuildChar(Arena* arena, StrBuilder* builder, char c) {
    __StrBuilderEnsureLargeEnough(arena, builder, 1);
    *(builder->buffer + builder->used) = c;
    builder->used++;
}

#define StrBuildF(arena, build, format, ...) (StrBuildFormat(arena, build, format, ## __VA_ARGS__))

void StrBuildF(Arena* arena, StrBuilder* builder, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    va_list ap2;
    va_copy(ap2, ap);
    int required = vsnprintf(NULL, 0, format, ap2);
    va_end(ap2);

    if (required >= 0) {
        __StrBuilderEnsureLargeEnough(arena, builder, (u64)required);
        vsnprintf(builder->buffer + builder->used, builder->capacity - builder->used, format, ap);
        builder->used += (u64)required;
    }
    va_end(ap);
}
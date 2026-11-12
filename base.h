#ifndef CLIB_BASE
#define CLIB_BASE
#include <stdint.h>
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
#define Clamp(x, a, b) (Min(Max(x, a), b))

#define FlagExists(_enum, i) (((_enum) & (i)) == (i))
#define FlagSet(_enum, i) ((_enum) |= (i))
#define FlagClear(_enum, i) ((_enum) &= ~(i))
#define FlagToggle(_enum, i) ((_enum) ^= (i))

#define ARR_CONST_LENGTH(A) (sizeof(A) / sizeof(*A))

#define ALIGN_OF(T) (sizeof(struct { u8 a; T b;}))
#define ALIGN_UP(p, alignment) (((p) + (alignment) - 1) & ~((alignment) - 1))

struct Arena;

// -------------  Errors --------------------

void LogError(const char* fmt, ...);

// -------------- Data Structures ------------

typedef struct {
    void* data;
    u32 count;
    u32 capacity;
} List;

#define LIST_DEFAULT_SIZE 64

#define ListAdd(type, arena, list, value) \
    do { \
    _ListEnsureLargeEnough(arena, list, sizeof(type), ALIGN_OF(type)); \
    *((type*)((list)->data) + ((list)->count++)) = value; \
    } while(0)

#define ListGet(type, list, index) \
    ((list)->count > 0 ? *((type*)((list)->data) + index) : 0)

#define ListRemove(type, list, index) \
    if ((list)->count > 0) \
        *((type*)((list)->data) + index) = *((type*)((list)->data) + (list)->count--)

#define ListClear(list) \
    (list)->count = 0

void _ListEnsureLargeEnough(struct Arena* arena, List* list, u32 size, u32 alignment);

// ------------- String ------------

typedef struct 
{
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

#define S(s) (FromCStr(s))

String FromCStr(const char * c_str);

char* StrToCStr(struct Arena* a, String s);
String StrSlice(String s, u32 start, u32 end);

typedef enum {
    STRFIND_FLAG_NONE,
    STRFIND_FLAG_SEARCH_BACKARDS
} STRFIND_FLAGS;

b32 StrFind(String s, char c, STRFIND_FLAGS flags, u32* index);
b32 StrEqualsCStr(String s, const char* c);
b32 CStrEqualsStr(const char* c, String s);
b32 StrEquals(String s1, String s2);

void StrCopy(struct Arena* arena, String from, String* into);
void StrBuildStr(struct Arena* arena, StrBuilder* builder, String s);
void StrBuildCStr(struct Arena* arena, StrBuilder* builder, const char* c_str);
void StrBuildChar(struct Arena* arena, StrBuilder* builder, char c);
#define StrBuildF(arena, build, format, ...) (StrBuildFormat(arena, build, format, ## __VA_ARGS__))
void StrBuildFormat(struct Arena* arena, StrBuilder* builder, const char* format, ...);

// impl

#ifdef CLIB_BASE_IMPL
#undef CLIB_BASE_IMPL
#include <platform.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

void _ListEnsureLargeEnough(struct Arena* arena, List* list, u32 size, u32 alignment) {
    if (!list->data) {
        if (list->capacity == 0) list->capacity = LIST_DEFAULT_SIZE;
        list->data = _ArenaPush(arena, size * list->capacity, alignment, false);
    }
    if (list->count == list->capacity) {
        list->capacity *= 2;
        void* new_data = _ArenaPush(arena, size * list->capacity, alignment, false);
        MemCopy(new_data, list->data, list->count * sizeof(size));
        list->data = new_data;
    }
}

// ----------- Strings ------------

String FromCStr(const char * c_str) {
    String s;
    s.ptr = c_str;
    s.size = strlen(c_str);
    return s;
}

char* StrToCStr(struct Arena* a, String s) {
    char* out = ArenaPushArray(a, char, s.size + 1);
    MemCopy(out, (char*)s.ptr, s.size);
    out[s.size] = '\0';
    return out;
}

String StrSlice(String s, u32 start, u32 end) {
    String out;
    out.size = end - start;
    out.ptr = s.ptr + start;
    return out;
}

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
    return s1.size == s2.size && memcmp(s1.ptr, s2.ptr, s1.size) == 0;
}

#define STR_BUILDER_DEFAULT_CAPACITY (128ull)

static void __StrBuilderEnsureLargeEnough(struct Arena* arena, StrBuilder* builder, u64 required) {
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

void StrCopy(struct Arena* arena, String from, String* to) {
    to->size = from.size;
    to->ptr = ArenaPushArray(arena, char, from.size);
}

void StrBuildStr(struct Arena* arena, StrBuilder* builder, String s) {
    __StrBuilderEnsureLargeEnough(arena, builder, s.size);
    memcpy(builder->buffer + builder->used, s.ptr, s.size);
    builder->used += s.size;
}

void StrBuildCStr(struct Arena* arena, StrBuilder* builder, const char* c_str) {
    StrBuildStr(arena, builder, FromCStr(c_str));
}

void StrBuildChar(struct Arena* arena, StrBuilder* builder, char c) {
    __StrBuilderEnsureLargeEnough(arena, builder, 1);
    *(builder->buffer + builder->used) = c;
    builder->used++;
}

#define StrBuildF(arena, build, format, ...) (StrBuildFormat(arena, build, format, ## __VA_ARGS__))

void StrBuildFormat(struct Arena* arena, StrBuilder* builder, const char* format, ...) {
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

#endif // CLIB_BASE_IMPLEMENTATION

#endif // CLIB_BASE

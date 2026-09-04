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

#define FlagExists(_enum, i) ((_enum) & (i) == (i))
#define FlagSet(_enum, i) ((_enum) |= (i))
#define FlagClear(_enum, i) ((_enum) &= ~(i))
#define FlagToggle(_enum, i) ((_enum) ^= (i))

#define ARR_CONST_LENGTH(A) (sizeof(A) / sizeof(*A))

#define ALIGN_OF(T) (sizeof(struct { u8 a; T b;}))

// -------------- Arena ------------

typedef struct Arena Arena;

#define ArenaPush(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), false))
#define ArenaPushZeroed(arena, type) (_ArenaPush((arena), sizeof(type), ALIGN_OF(type), true))
#define ArenaPushArray(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), false))
#define ArenaPushArrayZeroed(arena, type, n) (_ArenaPush((arena), sizeof(type) * (n), ALIGN_OF(type), true))

Arena* ArenaInit(u64 appMemory);
void* _ArenaPush(struct Arena* a, u64 size, u64 alignment, b32 clearToZero);
Arena* ArenaPushArena(Arena* arena, u64 size);
void ArenaReset(struct Arena* a, b32 decommit);
Arena* ArenaScratchInit(u64 size);
Arena* ArenaScratch(void);

// -------------- Data Structures ------------

typedef struct {
    void* data;
    u32 count;
    u32 capacity;
} List;

void ListAdd(List* l, u32 size, u32 alignment, ) {

}

// ------------- String ------------

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

#define S(s) (FromCStr(s))

String FromCStr(const char * c_str);

char* StrToCStr(Arena* a, String s);
String StrSlice(String s, u32 start, u32 end);

typedef enum {
    STRFIND_FLAG_NONE,
    STRFIND_FLAG_SEARCH_BACKARDS
} STRFIND_FLAGS;

b32 StrFind(String s, char c, STRFIND_FLAGS flags, u32* index);
b32 StrEqualsCStr(String s, const char* c);
b32 CStrEqualsStr(const char* c, String s);
b32 StrEquals(String s1, String s2);

void StrBuildStr(Arena* arena, StrBuilder* builder, String s);
void StrBuildCStr(Arena* arena, StrBuilder* builder, const char* c_str);
void StrBuildChar(Arena* arena, StrBuilder* builder, char c);
#define StrBuildF(arena, build, format, ...) (StrBuildFormat(arena, build, format, ## __VA_ARGS__))
void StrBuildFormat(Arena* arena, StrBuilder* builder, const char* format, ...);

// ---------------- Socket ----------------

//typedef struct Socket Socket;
//typedef struct Message Message; // defined by user, once message for all communication. Use tagged union

//Socket* ArenaPushSocket(Arena* arena);
//b32 SocketSendMessage(Message* msg);
//b32 SocketRecvMessage(Message* msg);

// impl

#ifdef CLIB_BASE_IMPLEMENTATION
#undef CLIB_BASE_IMPLEMENTATION
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// ----------- Strings ------------

String FromCStr(const char * c_str) {
    String s;
    s.ptr = c_str;
    s.size = strlen(c_str);
    return s;
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

void StrBuildFormat(Arena* arena, StrBuilder* builder, const char* format, ...) {
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
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

#define ALIGN_UP(p, alignment) (((p) + (alignment) - 1) & ~((alignment) - 1))

// indirection for nested macros with pasting
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)

#define overload __attribute__((overloadable))

struct Arena;

// -------------  Errors --------------------

void LogError(const char* fmt, ...);

// ------------- String ------------

typedef struct 
{
    u64 size; // in bytes
    const char* ptr;
} String;

typedef struct {
    union {
        struct {
            u64 used;
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

b32 StrFind(String s, char c, u64* index);
u32 StrSplit(struct Arena* arena, String s, char c, String** split);

String StrCopy(struct Arena* arena, String from);
void StrBuildStr(struct Arena* arena, StrBuilder* builder, String s);
void StrBuildCStr(struct Arena* arena, StrBuilder* builder, const char* c_str);
void StrBuildChar(struct Arena* arena, StrBuilder* builder, char c);
#define StrBuildF(arena, build, format, ...) (StrBuildFormat(arena, build, format, ## __VA_ARGS__))
void StrBuildFormat(struct Arena* arena, StrBuilder* builder, const char* format, ...);
u64 StrBuildMark(StrBuilder* builder);
void StrBuildResetTo(StrBuilder* builder, u64 to);
void StrBuildReset(StrBuilder* builder);

// -------------- Data Structures ------------

struct BitSet {
    u64 n_blocks;
    u64* blocks;
};

struct BitSet ArenaPushBitSet(struct Arena* arena, u64 n);

#define BitSet(b, i) ((b).blocks[(i) >> 9] |= (1ul << (i)))
#define BitUnset(b, i) ((b).blocks[(i) >> 9] &= ~(1ul << (i)))
#define BitToggle(b, i) ((b).blocks[(i) >> 9] ^= (1ul << (i)))
#define BitIsSet(b, i) (((b).blocks[(i) >> 9] & (1ul << (i))) != 0)

#define List(T) CAT(List_, T)
#define ListDef(T) \
    typedef struct List(T) { \
        u64 count, capacity; \
        T *data; \
    } List(T)

ListDef(i32); ListDef(i64); ListDef(u32); ListDef(u64); ListDef(String);

#define LIST_DEFAULT_SIZE 64

#define ListAdd(arena, list, value) \
    do { \
        if (!(list).data) { \
            if ((list).capacity == 0) (list).capacity = LIST_DEFAULT_SIZE; \
            (list).data = ArenaPushArray(arena, typeof(*(list).data), (list).capacity); \
        } \
        if ((list).count == (list).capacity) { \
            (list).capacity *= 2; \
            void* new_data = ArenaPushArray(arena, typeof(value), (list).capacity); \
            MemCopy(new_data, (list).data, (list).count * sizeof(value)); \
            (list).data = new_data; \
        } \
        (list).data[(list).count++] = (value); \
    } while(0)

#define ListGet(list, index) \
    ((list).count > 0 ? (list).data + (index) : 0)

#define ListRemove( list, index) \
    do { \
        if ((list).count > 0) \
            ((list).data[index] = (list).data[--(list).count]) \
    } while(0)

#define ListClear(list) \
    (list).count = 0

// Hashmap

// TODO:
overload u64 Hash(u64 x) { return x*31; }
overload u64 Hash(String str) { return 0;}

overload u64 Eq(u64 a, u64 b) { return a == b; }
overload b32 Eq(String a, String b) { 
    if (a.size != b.size) return false;
    for (u64 i = 0; i < a.size; ++i) if (a.ptr[i] != b.ptr[i]) return false;
    return true;
}

b32 EqScalar(u64 a, u64 b) { return a == b; }
b32 HashScalar(u64 x) { return x * 31; }

struct MapEntry { u64 hash, index; };
#define Map(K, V) CAT(CAT(Map_, K), CAT(_, V))
#define MapDef(K, V) \
    typedef struct Map(K, V) { \
        u64 count, capacity; \
        struct MapEntry* table; \
        struct { K key; V value; }* kvps; \
    } Map(K, V)

#define HM_EMPTY ~0ll
#define Fix(h_) ((h_) == HM_EMPTY ? 1ll : (h_))

#define MapPut(arena, map, k_, v_) \
    do { \
        if (!(map).table) { \
            if (!(map).capacity) (map).capacity = 1024; \
            (map).count = 0; \
            (map).table = ArenaPushArray(arena, struct MapEntry, (map).capacity); \
            MemFF((map).table, sizeof(struct MapEntry) * (map).capacity); \
            (map).kvps = _ArenaPush(arena, sizeof(*(map).kvps) * (map).capacity, __alignof__(*(map).kvps), false); \
        } \
        u64 hash__ = Hash(k_); hash__ = Fix(hash__); \
        u64 i__ = hash__ & ((map).capacity - 1); \
        for (u64 j__ = 0; j__ < (map).capacity; ++j__) { \
            if ((map).table[i__].hash == HM_EMPTY) { \
                if ((map).count >= (map).capacity * 3 / 4) { LogError("Hashmap full!"); break; } \
                (map).kvps[(map).count].key = (k_); \
                (map).kvps[(map).count].value = (v_); \
                (map).table[i__].hash = hash__; \
                (map).table[i__].index = (map).count; \
                (map).count++; \
                break; \
            } else if((map).table[i__].hash == hash__ && Eq((k_), (map).kvps[((map).table[i__].index)].key)) { \
                (map).kvps[(map).table[i__].index].value = (v_); \
                break; \
            } \
            i__ = (i__ + 1) & ((map).capacity - 1); \
        } \
    } while(0)

#define MapGet(map, k_) ({ \
        u64 r__ = ~0ll; \
        if ((map).table != 0) { \
        u64 hash__ = Hash(k_); hash__ = Fix(hash__); \
        u64 i__ = hash__ & ((map).capacity - 1); \
        for (u64 j__ = 0; j__ < (map).capacity; ++j__) { \
        struct MapEntry* e__ = (map).table + i__; \
        if (e__->hash == HM_EMPTY) break; \
        if(Eq((k_), (map).kvps[e__->index].key)) { \
        r__ = (e__->index); \
        break; \
        } \
        i__ = (i__ + 1) & ((map).capacity - 1); \
        } \
        } \
        r__ == ~0ll ? 0 : &(map).kvps[r__].value; })

#define MapGetOrPut(map, k_, v_) ({ \
        u64 r__ = ~0ll; \
        if (!(map).table) { \
        if (!(map).capacity) (map).capacity = 1024; \
        (map).count = 0; \
        (map).table = ArenaPushArray(arena, struct MapEntry, (map).capacity); \
        MemFF((map).table, sizeof(struct MapEntry) * (map).capacity); \
        (map).kvps = _ArenaPush(arena, sizeof(*(map).kvps) * (map).capacity, __alignof__(*(map).kvps), false); \
        } \
        u64 hash__ = Hash(k_); hash__ = Fix(hash__); \
        u64 i__ = hash__ & ((map).capacity - 1); \
        for (u64 j__ = 0; j__ < (map).capacity; ++j__) { \
        struct MapEntry* e__ = (map).table + i__; \
        if (e__->hash == HM_EMPTY) { \
        if ((map).count >= (map).capacity * 3 / 4) { LogError("Hashmap full!"); break; } \
        e__->hash = hash__; \
        e__->index = (map).count; \
        (map).kvps[(map).count].key = (k_); \
        (map).kvps[(map).count].value = (v_); \
        (map).count++; \
        r__ = e__->index; \
            break; \
        } \
            if(Eq((k_), (map).kvps[e__->index].key)) { \
                r__ = e__->index; \
                break; \
            } \
            i__ = (i__ + 1) & ((map).capacity - 1); \
        } \
    r__ == ~0ll ? 0 : &(map).kvps[r__].value; })

#define HashmapRemove(map, Tkey, Tvalue, key, hash_func, eq_func) \
    do { \
        u64 hash__ = hash_func(key); \
        u32 i__ = hash__ % (map).capacity; \
        while ((map).key_hashes[i__] == hash__) { \
            if (eq_func(((Tkey*)(map).keys)[i__], (key))) {  \
                u32 j__; \
                for (j__ = i__ + 1; (map).key_hashes[j__] == hash__; j__ = (j__ + 1) % (map).capacity) {} \
                j__ -= 1; \
                (map).key_hashes[j__] = HM_EMPTY; \
                if (j__ != i__) { \
                    ((Tkey*)(map).keys)[i__] = ((Tvalue*)(map).keys)[j__]; \
                    ((Tvalue*)(map).values)[i__] = ((Tvalue*)(map).values)[j__]; \
                } \
            } \
            i__ = (i__ + 1) % (map).capacity; \
        } \
    } while(0)

#define HashmapAdd(map, Tkey, Tvalue, key, value, hash_func, eq_func) \
    do { \
        u64 hash__ = hash_func(key); \
        u32 i__ = hash__ % (map).capacity; \
        while ((map).key_hashes[i__] == hash__) { \
            if (eq_func(((Tkey*)(map).keys)[i__], (key))) {  \
                (map).key_hashes[i__] = HM_EMPTY; \
                break; \
            } \
            i__ = (i__ + 1) % (map).capacity; \
        } \
        if ((map).key_hashes[i__] == HM_EMPTY) { \
            (map).key_hashes[i__] = hash__; \
            ((Tkey*)(map).keys)[i__] = key; \
            ((Tvalue*)(map).values)[i__] = value; \
        } else LogError("Hashmap full"); \
    } while(0)

#define Hashmap 

// impl

#ifdef CLIB_BASE_IMPL
#undef CLIB_BASE_IMPL
#include <platform.h>
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

b32 StrFind(String s, char c, u64* index) {
    for (u64 i = 0; i < s.size; ++i) if (s.ptr[i] == c) { *index = i; return true; }
    return false;
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

u32 StrSplit(struct Arena* arena, String s, char c, String** out_splits) {
    u32 n_splits = 0;
    u64 prev_index = 0;
    String* splits = NULL;
    for (u64 i = 0; i < s.size; ++i) {
        if (s.ptr[i] == c) { 
            u64 n_slice = i - prev_index;
            if (n_slice > 0) {
                String* split = ArenaPush(arena, String);
                if (!splits) splits = split;
                split->size = i - prev_index;
                split->ptr = s.ptr + prev_index;
                n_splits++;
            }
            prev_index = i + 1;
        }
    }
    if (prev_index < s.size) {
        String* split = ArenaPush(arena, String);
        if (!splits) splits = split;
        split->size = s.size - prev_index;
        split->ptr = s.ptr + prev_index;
        n_splits++;
    }

    *out_splits = splits;
    return n_splits;
}

String StrCopy(struct Arena* arena, String from) {
    char* p = ArenaPushArray(arena, char, from.size);
    MemCopy(p, (void*)from.ptr, from.size);
    return (String) {
        .ptr = p,
        .size = from.size
    };
}

void StrBuildStr(struct Arena* arena, StrBuilder* builder, String s) {
    __StrBuilderEnsureLargeEnough(arena, builder, s.size + 1);
    memcpy(builder->buffer + builder->used, s.ptr, s.size);
    builder->used += s.size;
    builder->buffer[builder->used] = '\0'; // in case we need it
}

void StrBuildCStr(struct Arena* arena, StrBuilder* builder, const char* c_str) {
    StrBuildStr(arena, builder, FromCStr(c_str));
}

void StrBuildChar(struct Arena* arena, StrBuilder* builder, char c) {
    __StrBuilderEnsureLargeEnough(arena, builder, 2);
    *(builder->buffer + builder->used) = c;
    builder->used++;
    builder->buffer[builder->used] = '\0'; // in case we need it
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
        __StrBuilderEnsureLargeEnough(arena, builder, (u64)required + 1);
        vsnprintf(builder->buffer + builder->used, builder->capacity - builder->used, format, ap);
        builder->used += (u64)required;
        builder->buffer[builder->used] = '\0'; // in case we need it
    }
    va_end(ap);
}

u64 StrBuildMark(StrBuilder* builder) { return builder->used; }
void StrBuildResetTo(StrBuilder* builder, u64 to) { builder->used = to; }
void StrBuildReset(StrBuilder* builder) { builder->used = 0; }

// Data structures

struct BitSet ArenaPushBitSet(struct Arena* arena, u64 n) {
    n = (n >> 9) + (n & 63lu) != 0;
    return (struct BitSet) {
        .n_blocks = n,
        .blocks = ArenaPushArrayZeroed(arena, u64, n)
    };
}


#endif // CLIB_BASE_IMPLEMENTATION

#endif // CLIB_BASE

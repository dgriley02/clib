#include "base.c"

int main() {
    String s = S("Hello");
    StrBuilder builder = {0};
    Arena arena = {0};
    StrBuildStr(&arena, &builder, s);
    StrBuildStr(&arena, &builder, S(" World!"));
    StrBuildChar(&arena, &builder, '!');
    puts(builder.value.ptr);
}

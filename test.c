#define CLIB_BASE_IMPLEMENTATION
#include "base.h" 
#include "graphics.h"
#include <stdio.h>

#define ARENA_GRAPHICS_SIZE MegaBytes(64)
#define ARENA_APP_SIZE ((ARENA_GRAPHICS_SIZE) + (MegaBytes(4)))

int main() {
    String s = S("Hello");
    StrBuilder builder = {0};
    Arena* arena_app = ArenaInit(ARENA_APP_SIZE); // default
    StrBuildStr(arena_app, &builder, s);
    StrBuildStr(arena_app, &builder, S(" World!"));
    StrBuildChar(arena_app, &builder, '!');
    Arena* arena_graphics = ArenaPushArena(arena_app, ARENA_GRAPHICS_SIZE);
    GConfig config = {
        .app_name = "This is my app",
        .version_info = (VersionInfo){1, 0, 0}
    };
    GInit(arena_app, &config);
    puts(builder.value.ptr);
}

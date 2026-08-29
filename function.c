#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "base.c"

#define function 

function void testing(
    int a, 
    int b)
{

}

function int other(int a, int b) {}

int main(int argc, char** argv) {
    Arena arena = {0};
    for (int i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "r");
        if (f == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }

        StrBuilder builder = {0};
        String filename = FromCStr(argv[i]);
        u32 dot;
        if (StrFind(filename, '.', 0, &dot)) {
            filename = StrSlice(filename, 0, dot);
        }
        StrBuildStr(&arena, &builder, filename);
        StrBuildStr(&arena, &builder, S("_gen_prototypes.h"));
        StrBuildChar(&arena, &builder, '\0');
        puts(builder.value.ptr);
        
        char* line = NULL; size_t n = 0; ssize_t n_read;
        while ((n_read = getline(&line, &n, f)) != -1) {
            builder.used = 0; // reset builder
            b32 found = 0;
            if (CStrEqualsStr(line, S("function"))) {
                u32 curly_open = -1;
                do {
                    StrBuildCStr(&arena, &builder, line);
                    if (StrFind(builder.value, '{', 0, &curly_open)) break;
                } while (getline(&line, &n, f) != -1);
                if (curly_open == -1) abort();

                // add comma at end of parentheses
                u32 end_parentheses;
                if (!StrFind(builder.value, ')', STRFIND_FLAG_SEARCH_BACKARDS, &end_parentheses)) {
                    abort();
                }
                String line_to_copy = StrSlice(builder.value, S("function").size, end_parentheses + 1);
                builder.used = 0;
                StrBuildStr(&arena, &builder, line_to_copy);
                StrBuildChar(&arena, &builder, ';');
                StrBuildChar(&arena, &builder, '\0');
                puts(builder.value.ptr);
            } else if (CStrEqualsStr(line, S("struct"))) {
                u32 curly_open_count = 0;
                b32 end = false;
                u32 first_curly_close = -1;
                do {
                    StrBuildCStr(&arena, &builder, line);
                    for (u32 i = 0; i < n; i++) {
                        if (line[i] == '{') curly_open_count++;
                        if (line[i] == '}') { end = true; first_curly_close = i; break; }
                    }
                } while (!end && getline(&line, &n, f) != -1);
                // I should just mmap the file, or somehow get a pointer to it so I can just read it normally. Fuck getting lines all the time.
                    
                do {
                    for
                }
                u32 curly_close_count = 0;
                do {
                    u32 discard;
                    if (StrFind(builder.value, '}', 0, &discard)) curly_close_count++;
                } while (curly_close_count < curly_open_count && getline(&line, &n, f) != -1);

                u32 last
            }
        }

        fclose(f);
    }
}

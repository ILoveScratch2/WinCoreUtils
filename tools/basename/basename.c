/* Basename tools for ILS WinCoreUtils.
   Copyright (C) 2025 ILoveScratch2 and WinCoreUtils contributors.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License v3 published by
   the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License 3 for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>. */

/* Written by WenDao(ILoveScratch2) ilovescratch@foxmail.com  */

#define _CRT_NONSTDC_NO_WARNINGS // Compatible for MSVC, GCC doesn't need this

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

char *PROGRAM_NAME ="basename";


#ifdef _WIN32
static UINT originalInputCP;
static UINT originalOutputCP;
#endif


#ifdef _WIN32
static void restoreCodePage(void);
#endif

// functions
static void usage(int status);
static void perform_basename(const char* string, const char* suffix, bool use_nuls);
static void remove_suffix(char* name, const char* suffix);
char* base_name(const char* path);
void strip_trailing_slashes(char* name);
bool is_absolute_path(const char* name);
bool is_root_directory(const char* name);

#ifdef _WIN32
static void restoreCodePage(void) {
    SetConsoleCP(originalInputCP);
    SetConsoleOutputCP(originalOutputCP);
}
#endif

static void usage(int status) {
    fprintf(stderr, "Usage: %s NAME [SUFFIX]\n", PROGRAM_NAME);
    fprintf(stderr, "   or: %s OPTION... NAME...\n", PROGRAM_NAME);
    fprintf(stderr, "Strip directory and suffix from FILE names.\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a, --multiple       support multiple arguments\n");
    fprintf(stderr, "  -s, --suffix=SUFFIX  remove SUFFIX\n");
    fprintf(stderr, "  -z, --zero           end output with NUL\n");
    fprintf(stderr, "  --help               display this help\n");
    fprintf(stderr, "  --version            output version\n");
    exit(status);
}

static void try_help(int status) {
    fprintf(stderr, "Try `basename --help' for more information.\n");
    exit(status);
}

char* base_name(const char* path) {
    if (!path || !*path) return strdup("");

    char* path_copy = strdup(path);
    char* p = path_copy;
    while (*p) {
        if (*p == '\\') *p = '/';
        p++;
    }

    char* last_slash = strrchr(path_copy, '/');
    char* result;

    if (!last_slash) {
        result = strdup(path_copy);
    }
    else {
        if (*(last_slash + 1) == '\0') {
            char* end = last_slash;
            while (end > path_copy && *end == '/') end--;

            if (end == path_copy && *end == '/') {
                result = strdup("/");
            }
            else {
                *(end + 1) = '\0';
                char* prev_slash = strrchr(path_copy, '/');
                result = prev_slash ? strdup(prev_slash + 1) : strdup(path_copy);
            }
        }
        else {
            result = strdup(last_slash + 1);
        }
    }

    free(path_copy);
    return result;
}

void strip_trailing_slashes(char* name) {
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == '/' || name[len - 1] == '\\')) {
        name[--len] = '\0';
    }
}

bool is_absolute_path(const char* name) {
    return (strlen(name) >= 3 && name[1] == ':' && (name[2] == '/' || name[2] == '\\')) ||
        (strlen(name) >= 2 && name[0] == '/' && name[1] == '/') ||
        (name[0] == '/' || name[0] == '\\');
}

bool is_root_directory(const char* name) {
    size_t len = strlen(name);
    return (len == 3 && name[1] == ':' && (name[2] == '/' || name[2] == '\\')) ||
        (len == 1 && (name[0] == '/' || name[0] == '\\')) ||
        (len == 2 && name[0] == '/' && name[1] == '/');
}

static void remove_suffix(char* name, const char* suffix) {
    char* np = name + strlen(name);
    const char* sp = suffix + strlen(suffix);

    while (np > name && sp > suffix)
        if (*--np != *--sp) return;

    if (np > name) *np = '\0';
}

static void perform_basename(const char* string, const char* suffix, bool use_nuls) {
    char* name = base_name(string);
    strip_trailing_slashes(name);

    if (suffix && !is_absolute_path(name) && !is_root_directory(name))
        remove_suffix(name, suffix);

    printf("%s%c", name, use_nuls ? '\0' : '\n');
    free(name);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    originalInputCP = GetConsoleCP();
    originalOutputCP = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    atexit(restoreCodePage);
#endif
    PROGRAM_NAME = argv[0];
    remove_suffix(PROGRAM_NAME, ".exe");

    bool multiple = false, use_nuls = false;
    const char* suffix = NULL;
    int optind = 1;

    while (optind < argc) {
        char *arg = argv[optind];
        if (arg[0] == '-') {
            if (strcmp(arg, "--") == 0) {
                optind++;
                break;
            } else if (strncmp(arg, "--", 2) == 0) {
                if (!strcmp(arg, "--multiple")) {
                    multiple = true;
                    optind++;
                } else if (!strcmp(arg, "--suffix")) {
                    if (++optind >= argc) {
                        fprintf(stderr, "%s: missing suffix\n", PROGRAM_NAME);
                        usage(EXIT_FAILURE);
                    }
                    suffix = argv[optind++];
                    multiple = true;
                } else if (strncmp(arg, "--suffix=", 9) == 0) {
                    suffix = arg + 9;
                    multiple = true;
                    optind++;
                } else if (!strcmp(arg, "--zero")) {
                    use_nuls = true;
                    optind++;
                } else if (!strcmp(arg, "--help")) {
                    usage(EXIT_SUCCESS);
                } else if (!strcmp(arg, "--version")) {
                    printf("ILS WinCoreUtils Basename 0.1.1\n");
                    exit(EXIT_SUCCESS);
                } else {
                    fprintf(stderr, "%s: invalid option '%s'\n", PROGRAM_NAME, arg);
                    try_help(EXIT_FAILURE);
                }
            } else {
                // 处理短选项组合
                for (int i = 1; arg[i] != '\0'; i++) {
                    char c = arg[i];
                    switch (c) {
                        case 'a':
                            multiple = true;
                            break;
                        case 's':
                            // 处理参数
                            if (arg[i + 1] != '\0') {
                                suffix = arg + i + 1;
                                i = strlen(arg) - 1; // 跳过剩余字符
                            } else {
                                optind++;
                                if (optind >= argc) {
                                    fprintf(stderr, "%s: missing suffix\n", PROGRAM_NAME);
                                    usage(EXIT_FAILURE);
                                }
                                suffix = argv[optind];
                            }
                            multiple = true;
                            break;
                        case 'z':
                            use_nuls = true;
                            break;
                        default:
                            fprintf(stderr, "%s: invalid option -- '%c'\n", PROGRAM_NAME, c);
                            try_help(EXIT_FAILURE);
                    }
                }
                optind++;
            }
        } else {
            break;
        }
    }

    int remaining = argc - optind;
    if (remaining < 1) {
        fprintf(stderr, "%s: missing operand\n", PROGRAM_NAME);
        try_help(EXIT_FAILURE);
    }

    if (multiple) {
        for (; optind < argc; optind++)
            perform_basename(argv[optind], suffix, use_nuls);
    } else {
        if (remaining > 2) {
            fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM_NAME, argv[optind + 2]);
            try_help(EXIT_FAILURE);
        }
        perform_basename(argv[optind], (remaining >= 2) ? argv[optind + 1] : NULL, use_nuls);
    }

    return EXIT_SUCCESS;
}
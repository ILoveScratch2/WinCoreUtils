#define _CRT_NONSTDC_NO_WARNINGS // Compatible for MSVC, GCC doesn't need this

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define PROGRAM_NAME "basename"


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

    bool multiple = false, use_nuls = false;
    const char* suffix = NULL;
    int optind = 1;

    while (optind < argc) {
        char* arg = argv[optind];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-a") || !strcmp(arg, "--multiple")) {
                multiple = true;
                optind++;
            }
            else if (!strcmp(arg, "-s") || !strcmp(arg, "--suffix")) {
                if (++optind >= argc) {
                    fprintf(stderr, "basename: missing suffix\n");
                    usage(EXIT_FAILURE);
                }
                suffix = argv[optind++];
                multiple = true;
            }
            else if (strncmp(arg, "--suffix=", 9) == 0) {
                suffix = arg + 9;
                multiple = true;
                optind++;
            }
            else if (!strcmp(arg, "-z") || !strcmp(arg, "--zero")) {
                use_nuls = true;
                optind++;
            }
            else if (!strcmp(arg, "--help")) {
                usage(EXIT_SUCCESS);
            }
            else if (!strcmp(arg, "--version")) {
                printf("ILS WinCoreUtils Basename 0.1.1\n");
                exit(EXIT_SUCCESS);
            }
            else {
                fprintf(stderr, "basename: invalid option '%s'\n", arg);
                try_help(EXIT_FAILURE);
            }
        }
        else break;
    }

    int remaining = argc - optind;
    if (remaining < 1) {
        fprintf(stderr, "basename: missing operand\n");
        try_help(EXIT_FAILURE);
    }

    if (multiple) {
        for (; optind < argc; optind++)
            perform_basename(argv[optind], suffix, use_nuls);
    }
    else {
        if (remaining > 2) {
            fprintf(stderr, "basename: extra operand '%s'\n", argv[optind + 2]);
            try_help(EXIT_FAILURE);
        }
        perform_basename(argv[optind], (remaining >= 2) ? argv[optind + 1] : NULL, use_nuls);
    }

    return EXIT_SUCCESS;
}
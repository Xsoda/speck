/*
The MIT License (MIT)

Copyright (c) 2015 Andreas Pfohl

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>

/* Constants */

static const struct {
    char *red;
    char *green;
    char *yellow;
    char *blue;
    char *magenta;
    char *cyan;
    char *white;
    char *normal;
} colors = {
    .red     = "\x1B[31m",
    .green   = "\x1B[32m",
    .yellow  = "\x1B[33m",
    .blue    = "\x1B[34m",
    .magenta = "\x1B[35m",
    .cyan    = "\x1B[36m",
    .white   = "\x1B[37m",
    .normal  = "\x1B[0m"
};

/* Data structures */

struct state {
    int index;
    char **assertions;
    int *codes;
    char *function;
};

struct suite {
    char *name;
    char *c_file;
    char *so_file;
    void *handle;
    char **tests;
    struct state **states;
};

struct statistic {
    char *symbols;
    char **failures;
    int flag;
};

/* Helper functions */

char *string_dup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *new_str = malloc(len * sizeof(char));
    if (new_str) {
        memcpy(new_str, str, len);
    }

    return new_str;
}

int alloc_sprintf(char **str, const char *format, ...)
{
    const int start_size = 8;
    *str = malloc(start_size * sizeof(char));
    va_list ap, ap_copy;
    va_start(ap, format);
    va_copy(ap_copy, ap);
    int size = vsnprintf(*str, start_size, format, ap);
    va_end(ap);
    if (size > start_size - 1) {
        *str = realloc(*str, (size + 1) * sizeof(char));
        vsnprintf(*str, size + 1, format, ap_copy);
        va_end(ap_copy);
    }

    return size;
}

/* char *str_match(const char text[], size_t textlen) */
/* { */
/*     char str[] = "void spec_"; */
/*     int len = 10; */

/*     if (textlen >= len) { */
/*         for (int i = 0; i < len; i++) { */
/*             if (str[i] != text[i]) { */
/*                 return NULL; */
/*             } */
/*         } */

/*         int pre_offset = 5; /\* "void " *\/ */
/*         int post_offset = 7; /\* "(void)\n" *\/ */

/*         char *match = malloc((textlen - pre_offset - post_offset + 1) * sizeof(char)); */

/*         memcpy(match, text + pre_offset, textlen - pre_offset - post_offset); */
/*         match[textlen - pre_offset - post_offset] = '\0'; */

/*         return match; */
/*     } */

/*     return NULL; */
/* } */

char *str_match(const char text[], size_t textlen) {
   char *match, *end;
   char *ptr = text;
   while (ptr && isspace(*(unsigned char *)ptr)) ptr++;
   if (strncmp(ptr, "void", 4)) return NULL;
   ptr += 4;
   while (ptr && isspace(*(unsigned char *)ptr)) ptr++;
   if (strncmp(ptr, "spec_", 5)) return NULL;
   match = ptr;
   while (ptr && !isspace(*(unsigned char *)ptr) && *ptr != '(') ptr++;
   end = ptr;
   ptr = (char *)malloc(end - match + 1);
   memcpy(ptr, match, end - match);
   ptr[end - match] = 0;
   return ptr;
}

clock_t start_watch()
{
    return clock();
}

void stop_watch(clock_t watch, const char *name)
{
    printf("\n%s took %f seconds.\n", name, (double)(clock() - watch) / CLOCKS_PER_SEC);
}

/* Control functions */

void get_tests(struct suite *suite)
{
    FILE *fp = fopen(suite->c_file, "r");
    char line[1024];
    size_t linelen = 1024;
    ssize_t len;
    int test_count = 0;
    while (fgets(line, linelen, fp)) {
        char *temp = str_match(line, len);
        if (temp) {
            suite->tests = realloc(suite->tests, (test_count + 1) * sizeof(char *));
            suite->tests[test_count++] = temp;
        }
    }

    suite->tests = realloc(suite->tests, (test_count + 1) * sizeof(char *));
    suite->tests[test_count] = NULL;

    fclose(fp);
}

void load_suite(struct suite *suite)
{
    suite->handle = dlopen(suite->so_file, RTLD_LAZY);

    get_tests(suite);
}

void run_tests(struct suite *suite)
{
    int i = 0;
    for (; suite->tests[i] != NULL; i++) {
        struct state *state = dlsym(suite->handle, "state");
        state->index = 0;
        state->codes = NULL;
        state->assertions = NULL;
        state->function = suite->tests[i];

        void (*test)(void) = dlsym(suite->handle, suite->tests[i]);
        if (test) {
           test();
        } else {
           fprintf(stderr, "Can't get function `%s' address at `%s'.\n", suite->tests[i], suite->so_file);
           assert(0);
        }

        suite->states = realloc(suite->states, (i + 1) * sizeof(struct state *));
        suite->states[i] = malloc(sizeof(struct state));
        suite->states[i]->index = state->index;
        suite->states[i]->assertions = state->assertions;
        suite->states[i]->codes = state->codes;
    }

    suite->states = realloc(suite->states, (i + 1) * sizeof(struct state *));
    suite->states[i] = NULL;
}

struct suite **get_suites(void)
{
    char spec_dir[] = "spec";
    struct suite **suites = NULL;

    DIR *directory = opendir(spec_dir);
    struct dirent *entry = NULL;

    int count = 0;
    while ((entry = readdir(directory)) != NULL) {
        char *found = strstr(entry->d_name, ".so");
        if (found != NULL && found[3] == '\0') {
            count++;
            suites = realloc(suites, count * sizeof(struct suite *));
            suites[count - 1] = malloc(sizeof(struct suite));

            size_t len = found - entry->d_name;
            char *base_name = malloc((len + 1) * sizeof(char));

            memcpy(base_name, entry->d_name, len);
            base_name[len] = '\0';

            alloc_sprintf(&(suites[count - 1]->c_file), "%s/%s.c", spec_dir, base_name);
            alloc_sprintf(&(suites[count - 1]->so_file), "%s/%s.so", spec_dir, base_name);
            suites[count - 1]->name = base_name;

            suites[count - 1]->tests = NULL;

            suites[count - 1]->states = NULL;
        }
    }

    closedir(directory);

    count++;
    suites = realloc(suites, count * sizeof(struct suite *));

    suites[count - 1] = NULL;

    return suites;
}

void free_suites(struct suite **suites)
{
    for (int i = 0; suites[i] != NULL; i++) {
        free(suites[i]->name);
        free(suites[i]->c_file);
        free(suites[i]->so_file);


        dlclose(suites[i]->handle);

        for (int j = 0; suites[i]->tests[j] != NULL; j++) {
            free(suites[i]->tests[j]);
        }
        free(suites[i]->tests);

        if (suites[i]->states) {
            for (int j = 0; suites[i]->states[j] != NULL; j++) {
                if (suites[i]->states[j]->codes) {
                    free(suites[i]->states[j]->codes);
                }
                if (suites[i]->states[j]->assertions) {
                    for (int k = 0; k < suites[i]->states[j]->index; k++) {
                        free(suites[i]->states[j]->assertions[k]);
                    }
                    free(suites[i]->states[j]->assertions);
                }
                free(suites[i]->states[j]);
            }
            free(suites[i]->states);
        }

        free(suites[i]);
    }

    free(suites);
}

/* Statistic */

struct statistic *build_statistic(struct suite **suites)
{
    struct statistic *statistic = malloc(sizeof(struct statistic));
    statistic->symbols = malloc(sizeof(char));
    statistic->symbols[0] = '\0';
    statistic->failures = NULL;
    statistic->flag = 0;

    int index = 0;

    for (int suite = 0; suites[suite] != NULL; suite++) {
        for (int state = 0; suites[suite]->states[state] != NULL; state++) {
            for (int assertion = 0; assertion < suites[suite]->states[state]->index; assertion++) {
                size_t length = strlen(statistic->symbols);
                statistic->symbols = realloc(statistic->symbols, (length + 5 + 1 + 1) * sizeof(char));
                statistic->failures = realloc(statistic->failures, (index + 2) * sizeof(char *));
                statistic->failures[index] = NULL;
                statistic->failures[index + 1] = NULL;

                if (suites[suite]->states[state]->codes[assertion] == 0) {
                    sprintf(statistic->symbols + length, "%s.", colors.green);
                    alloc_sprintf(&(statistic->failures[index]), "");
                } else {
                    sprintf(statistic->symbols + length, "%sF", colors.red);
                    alloc_sprintf(&(statistic->failures[index]), "  - %s.c%s", suites[suite]->name, suites[suite]->states[state]->assertions[assertion]);
                    statistic->flag = 1;
                }

                index++;
            }
        }
    }

    return statistic;
}

void print_statistic(struct statistic *statistic)
{
    printf("%s\n", statistic->symbols);

    if (statistic->flag) {
        printf("\n%sFailures:\n", colors.red);

        for (int failure = 0; statistic->failures[failure] != NULL; failure++) {
            if (strlen(statistic->failures[failure]) > 0) {
                printf("%s\n", statistic->failures[failure]);
            }
        }
    }

    printf("%s", colors.normal);
}

void free_statistic(struct statistic *statistic)
{
    if (statistic) {
        if (statistic->symbols) {
            free(statistic->symbols);
        }

        if (statistic->failures) {
            for (int failure = 0; statistic->failures[failure] != NULL; failure++) {
                free(statistic->failures[failure]);
            }
            free(statistic->failures);
        }

        free(statistic);
    }
}

/* Main */

int main(int argc, char **argv)
{
    time_t watch = start_watch();

    int exit_code = EXIT_SUCCESS;
    struct suite **suites = get_suites();

    for (int suite = 0; suites[suite] != NULL; suite++) {
        load_suite(suites[suite]);
        run_tests(suites[suite]);
    }

    struct statistic *statistic = build_statistic(suites);

    if (statistic->flag) {
        exit_code = EXIT_FAILURE;
    }

    print_statistic(statistic);
    free_statistic(statistic);

    free_suites(suites);

    stop_watch(watch, "Running all suites");

    return exit_code;
}

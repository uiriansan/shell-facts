#include "cjson/cJSON.h"
#include <ctype.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DB_PATH "/facts.db"
#define IMAGE_MAX_LINES 50
#define IMAGE_MAX_LINE_LENGTH 512
#define IMAGE_TEMP_FILE "/tmp/shell-facts-XXXXXX"

typedef struct {
    char *text;
    char *thumb;
    int year;
    cJSON *pages;
} Fact;

char *resolve_db_path() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        return strcat(dir, DB_PATH);
    } else {
        return NULL;
    }
}

void wrap_text_by_words(char *text, size_t cols, size_t prefix_size) {
    size_t last_space = 0;
    size_t col_count = prefix_size;
    char *result;
    for (size_t i = 0; i < strlen(text); i++) {
        if (text[i] == ' ') {
            last_space = i;
        } else if (col_count + 1 > cols && last_space > 0) {
            text[last_space] = '\n';
            col_count = prefix_size;
            last_space = 0;
        }
        col_count += 1;
    }
}

void strip_title(char *title) {
    for (size_t i = 0; i < strlen(title); i++) {
        if (title[i] == '_')
            title[i] = ' ';
    }
}

size_t get_thumbnail_sequence(char *thumb, char ***image_buffer, char *chafa_options) {
    FILE *fp;
    char buf[IMAGE_MAX_LINE_LENGTH];
    char **lines = NULL;
    size_t line_count = 0;

    char image_temp_file[256];
    strcpy(image_temp_file, IMAGE_TEMP_FILE);
    int fd = mkstemp(image_temp_file);
    if (fd == -1) {
        fprintf(stderr, "Failed to create temp file!\n");
        return -1;
    }
    close(fd);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s --max-time 5 -o \"%s\" \"%s\"", image_temp_file, thumb);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to download image file!\n");
        unlink(image_temp_file);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "chafa %s \"%s\"", chafa_options, image_temp_file);

    // Alternatively, pipe the image data directly into chafa without a temp file:
    // snprintf(cmd, sizeof(cmd), "curl -s \"%s\" | chafa %s", thumb, chafa_options);

    fp = popen(cmd, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to retrieve image data!\n");
        unlink(image_temp_file);
        return -1;
    }

    lines = malloc(IMAGE_MAX_LINES * sizeof(char *));
    if (lines == NULL) {
        fprintf(stderr, "Failed to allocate memory for image data!\n");
        pclose(fp);
        unlink(image_temp_file);
        return -1;
    }

    char buffer[4096];

    size_t br;
    while ((br = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        // size_t len = strlen(buf);
        // lines[line_count] = malloc((strlen(buf) + 1) * sizeof(char));
        fwrite(buffer, 1, br, stdout); // Write directly

        // if (lines[line_count] == NULL) {
        //     fprintf(stderr, "Failed to allocate memory for image data!\n");
        //     for (size_t i = 0; i < line_count; i++) {
        //         free(lines[i]);
        //     }
        //     free(lines);
        //     pclose(fp);
        //     unlink(image_temp_file);
        //     return -1;
        // }
        // strcpy(lines[line_count], buf);

        line_count++;
    }
    printf("\033_Gq=1\033\\"); // Clear all Kitty graphics
    fflush(stdout);

    size_t lc = 0;
    for (size_t i = 0; i < 4096; i++) {
        if (buffer[i] == '\n')
            lc++;
    }
    if (lc > 1) {
    } // --format=symbols
    else {
    } // --format=kitty
    printf("lines: %zu\n", lc);

    pclose(fp);
    unlink(image_temp_file);
    *image_buffer = lines;
    return line_count;
}

char *to_lower(char *str) {
    for (char *p = str; *p; p++) {
        *p = tolower(*p);
    }
    return str;
}

void print_raw(Fact fact) {
    char *pages_string;
    if (cJSON_IsArray(fact.pages)) {
        pages_string = cJSON_Print(fact.pages);
    }
    cJSON_Minify(pages_string);
    printf("%s||%s||%d||%s\n", fact.text, fact.thumb, fact.year, pages_string);
    free(pages_string);
}

void print_fact(Fact fact) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    size_t image_width = 20;

    char *sb[image_width + 1000];

    wrap_text_by_words(fact.text, w.ws_col, image_width);
    // printf("%s\nYear: %d%s\n", fact.text, abs(fact.year),
    //        fact.year < 0 ? " BC" : "");
    printf("%s\n", fact.text);

    if (cJSON_IsArray(fact.pages)) {
        cJSON *page, *thumb;
        size_t i = 0;
        size_t char_count = image_width + 5;
        cJSON_ArrayForEach(page, fact.pages) {
            if (i == 0) {
                thumb = cJSON_GetObjectItemCaseSensitive(page, "thumb");
                for (size_t r = 0; r < char_count - 5; r++) {
                    printf(" ");
                }
                printf("See: ");
            }

            cJSON *t_title = cJSON_GetObjectItemCaseSensitive(page, "title");
            cJSON *url = cJSON_GetObjectItemCaseSensitive(page, "url");
            if (cJSON_IsString(t_title) && *t_title->valuestring &&
                cJSON_IsString(url) && *url->valuestring) {
                char *title = t_title->valuestring;
                strip_title(title);

                if (char_count + strlen(title) + 3 >= w.ws_col) {
                    char_count = image_width + 5;
                    printf("\n");
                    for (size_t r = 0; r < char_count; r++) {
                        printf(" ");
                    }
                } else if (i > 0) {
                    printf(" • ");
                    char_count += 3;
                }
                // Blue, italic, underlined hyperlink
                printf("\033[34;3;4m\e]8;;%s\e\\%s\e]8;;\e\\\033[0m", url->valuestring, title);
                char_count += strlen(title);
            }
            i++;
        }
        printf("\n");

        char **image_lines = NULL;
        size_t image_line_count = 0;
        if (cJSON_IsString(thumb) && *thumb->valuestring) {
            image_line_count = get_thumbnail_sequence(thumb->valuestring, &image_lines, "--size=30x10");
        }

        if (image_line_count < 0) {
            fprintf(stderr, "Failed to retrieve image data!\n");
            exit(1);
        }
        printf("%zu\n", image_line_count);
        for (size_t i = 0; i < image_line_count; i++) {
            size_t br;
            free(image_lines[i]);
        }
        free(image_lines);
    }
    printf("Term size: %dx%d\n", w.ws_row, w.ws_col);
}

void print_cli_usage() {
    printf("-h, --help           -> Prints this message.\n"
           "-r, --raw            -> Outputs raw data separated by '||'\n"
           "-d, --db-path <path> -> Changes the path to the databse.\n"
           "                        By default, the program will look for 'facts.db'\n"
           "                        in the same directory as the executable.\n"
           "-t, --type <type>    -> The type of fact to be displayed.\n"
           "                        Options are: selected, births, deaths, events and holidays.\n"
           "                        Default is 'selected'.\n");
}

static struct option cli_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"raw", no_argument, NULL, 'r'},
    {"db-path", required_argument, NULL, 'd'},
    {"type", required_argument, NULL, 't'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv) {
    uint8_t output_raw = 0;
    char *output_type = "selected";
    char *db_path = resolve_db_path();

    int ch;
    while ((ch = getopt_long(argc, argv, "hrd:t:", cli_options, NULL)) != -1) {
        if (ch == -1)
            break;

        switch (ch) {
        case 'r':
            output_raw = 1;
            break;
        case 'd':
            if (access(optarg, F_OK) == 0 && strcmp((char *)optarg + strlen(optarg) - 3, ".db") == 0) {
                db_path = optarg;
            } else {
                printf("`db-path` is not a valid sqlite .db file.\n");
                exit(1);
            }
            break;
        case 't':
            if (strcmp(to_lower(optarg), "selected") == 0 ||
                strcmp(to_lower(optarg), "births") == 0 ||
                strcmp(to_lower(optarg), "deaths") == 0 ||
                strcmp(to_lower(optarg), "events") == 0 ||
                strcmp(to_lower(optarg), "holidays") == 0) {
                output_type = optarg;
            } else {
                printf("Invalid type `%s`. Valid types are `selected, births, deaths, events and holidays`\n\n", optarg);
            }
            break;
        case 'h':
        default:
            print_cli_usage();
            exit(1);
        }
    }

    if (db_path == NULL) {
        fprintf(stderr, "[ERROR]: Could not resolve DB_PATH.\n");
        return 1;
    }

    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR]: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT text, thumb, year, pages FROM Facts WHERE type LIKE "
                      "? AND day LIKE ? AND month LIKE "
                      "? ORDER BY RANDOM() LIMIT 1;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR]: Failed to prepare SQL query: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    sqlite3_bind_text(stmt, 1, output_type, strlen(output_type), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, tm_info->tm_mday);
    sqlite3_bind_int(stmt, 3, tm_info->tm_mon + 1);

    Fact fact = {.text = "", .year = 0};

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        fact.text = strdup((const char *)sqlite3_column_text(stmt, 0));
        fact.thumb = strdup((const char *)sqlite3_column_text(stmt, 1));
        fact.year = sqlite3_column_int(stmt, 2);
        char *pages_str = strdup((const char *)sqlite3_column_text(stmt, 3));
        fact.pages = cJSON_Parse(pages_str);
        free(pages_str);
        if (fact.pages == NULL) {
            fprintf(stderr, "[ERROR]: Failed to parse pages: %s\n", cJSON_GetErrorPtr());
        }
    } else {
        fprintf(stderr, "No facts today :/.\n");
        sqlite3_close(db);
        free(fact.text);
        free(fact.thumb);
        cJSON_Delete(fact.pages);
        return 1;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (output_raw)
        print_raw(fact);
    else
        print_fact(fact);

    free(fact.text);
    free(fact.thumb);
    cJSON_Delete(fact.pages);
    return 0;
}

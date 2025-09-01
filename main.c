#include "cjson/cJSON.h"
#include <chafa/chafa.h>
#include <ctype.h>
#include <getopt.h>
#include <glib-2.0/glib.h>
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
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DB_PATH "/facts.db"
#define IMAGE_MAX_LINES 50
#define IMAGE_MAX_LINE_LENGTH 512
#define IMAGE_TEMP_FILE "/tmp/shell-facts-XXXXXX"
#define MAX_IMAGE_WIDTH 30
#define MAX_IMAGE_HEIGHT 10

typedef struct {
    char *text;
    char *thumb;
    int t_width;
    int t_height;
    int year;
    cJSON *pages;
} Fact;

typedef struct {
    uint8_t output_raw;
    uint8_t render_image;
    char *db_path;
    char *fact_type;
    int day;
    int month;

    int term_rows;
    int term_cols;
} CmdOptions;

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

void strip_title(char *title) {
    for (size_t i = 0; i < strlen(title); i++) {
        if (title[i] == '_')
            title[i] = ' ';
    }
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
    printf("%s||%s||%d||%d||%d||%s\n", fact.text, fact.thumb, fact.t_width, fact.t_height, fact.year, pages_string);
    free(pages_string);
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
    {"no-img", no_argument, NULL, 'i'},
    {"db-path", required_argument, NULL, 'p'},
    {"type", required_argument, NULL, 't'},
    {"day", required_argument, NULL, 'd'},
    {"month", required_argument, NULL, 'm'},
    {NULL, 0, NULL, 0}};

CmdOptions parse_cmdline(int argc, char **argv) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    struct winsize term_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term_size);

    CmdOptions options = {
        .output_raw = 0,
        .render_image = 1,
        .db_path = resolve_db_path(),
        .fact_type = "selected",
        .day = tm_info->tm_mday,
        .month = tm_info->tm_mon + 1,

        .term_rows = term_size.ws_row,
        .term_cols = term_size.ws_col,
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "hrid:t:d:m:", cli_options, NULL)) != -1) {
        if (ch == -1)
            break;

        switch (ch) {
        case 'r':
            options.output_raw = 1;
            break;
        case 'i':
            options.render_image = 0;
            break;
        case 'p':
            if (access(optarg, F_OK) == 0 && strcmp((char *)optarg + strlen(optarg) - 3, ".db") == 0) {
                options.db_path = optarg;
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
                options.fact_type = optarg;
            } else {
                printf("Invalid type `%s`. Valid types are `selected, births, deaths, events and holidays`\n\n", optarg);
            }
            break;
        case 'd':
            options.day = atoi(optarg);
            break;
        case 'm':
            options.month = atoi(optarg);
            break;
        case 'h':
        default:
            print_cli_usage();
            exit(1);
        }
    }

    // clang-format off
	int month_days[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    // clang-format on
    if (options.month < 1 || options.month > 12 || options.day < 1 || options.day > month_days[options.month - 1]) {
        fprintf(stderr, "%02d/%02d is not a valid date.\n", options.day, options.month);
        exit(1);
    }

    if (options.db_path == NULL) {
        fprintf(stderr, "[ERROR]: Could not resolve DB_PATH.\n");
        exit(1);
    }
    return options;
}

Fact query_data(CmdOptions options, char *type, int day, int month) {
    sqlite3 *db;
    int rc = sqlite3_open(options.db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR]: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT text, thumb, thumb_w, thumb_h, year, pages FROM Facts WHERE type LIKE "
                      "? AND day LIKE ? AND month LIKE "
                      "? ORDER BY RANDOM() LIMIT 1;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR]: Failed to prepare SQL query: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    sqlite3_bind_text(stmt, 1, options.fact_type, strlen(options.fact_type), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, day);
    sqlite3_bind_int(stmt, 3, month);

    Fact fact = {
        .text = "",
        .thumb = "",
        .t_width = 0,
        .t_height = 0,
        .year = 0,
    };

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        fact.text = strdup((const char *)sqlite3_column_text(stmt, 0));
        fact.thumb = strdup((const char *)sqlite3_column_text(stmt, 1));
        fact.t_width = sqlite3_column_int(stmt, 2);
        fact.t_height = sqlite3_column_int(stmt, 3);
        fact.year = sqlite3_column_int(stmt, 4);
        char *pages_str = strdup((const char *)sqlite3_column_text(stmt, 5));
        fact.pages = cJSON_Parse(pages_str);
        free(pages_str);
        if (fact.pages == NULL) {
            fprintf(stderr, "[ERROR]: Failed to parse pages: %s\n", cJSON_GetErrorPtr());
        }
    } else {
        fprintf(stderr, "No facts today :/.\n");
        sqlite3_close(db);
        exit(1);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return fact;
}

void calculate_canvas_size(int img_width, int img_height,
                           int max_cols, int max_rows,
                           int *canvas_cols, int *canvas_rows) {
    double img_aspect = (double)img_width / img_height;
    double max_aspect = (double)max_cols / max_rows;

    if (img_aspect > max_aspect) {
        // Image is wider - limit by columns
        *canvas_cols = max_cols;
        *canvas_rows = (int)(max_cols / img_aspect);
    } else {
        // Image is taller - limit by rows
        *canvas_rows = max_rows;
        *canvas_cols = (int)(max_rows * img_aspect);
    }
}

uint8_t render_thumb(char *thumb, int width, int height, int term_width, int term_height) {
    if (MAX_IMAGE_WIDTH * 3 > term_width || MAX_IMAGE_HEIGHT * 3 > term_height)
        return 0;

    char image_temp_file[256] = IMAGE_TEMP_FILE;
    int fd = mkstemp(image_temp_file);
    if (fd == -1) {
        fprintf(stderr, "Failed to create temp file!\n");
        return 0;
    }
    close(fd);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s --max-time 5 -o \"%s\" \"%s\"", image_temp_file, thumb);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to download image file!\n");
        unlink(image_temp_file);
        return 0;
    }

    int img_width, img_height, channel_c;
    unsigned char *image_data = stbi_load(image_temp_file, &img_width, &img_height, &channel_c, STBI_rgb_alpha);
    if (image_data == NULL) {
        fprintf(stderr, "Failed to load image file!\n");
        unlink(image_temp_file);
        return 0;
    }

    ChafaCanvasConfig *config = chafa_canvas_config_new();
    int canvas_w = 30, canvas_h = 10;
    chafa_calc_canvas_geometry(img_width, img_height, &canvas_w, &canvas_h, 0.5, FALSE, FALSE);
    chafa_canvas_config_set_geometry(config, canvas_w, canvas_h);
    chafa_canvas_config_set_pixel_mode(config, CHAFA_PIXEL_MODE_KITTY);
    // chafa_canvas_config_set_canvas_mode(config, CHAFA_CANVAS_MODE_TRUECOLOR);
    // chafa_canvas_config_set_work_factor(config, 1.0);
    // chafa_canvas_config_set_optimizations(config, CHAFA_OPTIMIZATION_ALL);
    ChafaCanvas *canvas = chafa_canvas_new(config);

    if (canvas == NULL) {
        fprintf(stderr, "Failed to render image file!\n");
        stbi_image_free(image_data);
        return 0;
    }

    chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, image_data, img_width, img_height, img_width * 4);
    GString *g_string = chafa_canvas_print(canvas, NULL);
    if (g_string != NULL) {
        printf("%s\n", g_string->str);
        g_string_free(g_string, TRUE);
    } else {
        fprintf(stderr, "Failed to render image file!\n");
        return 0;
    }

    chafa_canvas_unref(canvas);
    chafa_canvas_config_unref(config);
    stbi_image_free(image_data);

    // snprintf(cmd, sizeof(cmd), "chafa --size=%dx%d --view-size=%dx%d --relative=on --fit-width --align='center,center' \"%s\"", MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT, MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT, image_temp_file);
    // // Alternatively, pipe the image data directly into chafa without a temp file:
    // // snprintf(cmd, sizeof(cmd), "curl -s \"%s\" | chafa --size=30x10", thumb);
    //
    // if (system(cmd) != 0) {
    //     fprintf(stderr, "Failed to render image file!\n");
    //     unlink(image_temp_file);
    //     return 0;
    // }

    unlink(image_temp_file);
    return 1;
}

void print_fact(Fact fact, uint8_t image_rendered, int term_width, int term_height) {
    int initial_col = 0, initial_row = 0;

    if (image_rendered)
        initial_col = MAX_IMAGE_WIDTH + 2;

    // Save cursor position:
    printf("\033[s");
    // Move cursor to the right:
    printf("\033[%dC", initial_col);
    // Move cursor up:
    printf("\033[%dA", MAX_IMAGE_HEIGHT);
    printf("Hey, there!");
    // Restore cursor position:
    printf("\033[u");
}

int main(int argc, char **argv) {
    CmdOptions options = parse_cmdline(argc, argv);

    Fact fact = query_data(options, options.fact_type, options.day, options.month);

    if (options.output_raw)
        print_raw(fact);
    else {
        uint8_t image_rendered = 0;
        if (options.render_image && fact.thumb && strlen(fact.thumb) > 0) {
            image_rendered = render_thumb(fact.thumb, fact.t_width, fact.t_height, options.term_cols, options.term_rows);
        }
        print_fact(fact, image_rendered, options.term_cols, options.term_rows);
    }
    free(fact.text);
    free(fact.thumb);
    cJSON_Delete(fact.pages);

    return 0;
}

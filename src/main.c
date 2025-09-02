#include "vendor/cjson/cJSON.h"
#include <termios.h>
#define DS_SB_IMPLEMENTATION
#include "string_buffer.h"
#include <asm-generic/ioctls.h>
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
#include "vendor/stb_image.h"

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
    int day;
    int month;
    int year;
    cJSON *pages;
} Fact;

typedef struct {
    gint width_cells, height_cells;
    gint width_pixels, height_pixels;
} TermSize;

typedef struct {
    uint8_t output_raw;
    uint8_t render_image;
    char *db_path;
    char *fact_type;
    int day;
    int month;

    TermSize term_size;
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

void get_term_size(TermSize *term_size_out) {
    TermSize term_size;
    term_size.width_cells = term_size.height_cells = term_size.width_pixels = term_size.height_pixels = -1;

    struct winsize w;
    gboolean have_winsz = FALSE;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) >= 0 || ioctl(STDERR_FILENO, TIOCGWINSZ, &w) >= 0 || ioctl(STDIN_FILENO, TIOCGWINSZ, &w) >= 0)
        have_winsz = TRUE;

    if (have_winsz) {
        term_size.width_cells = w.ws_col;
        term_size.height_cells = w.ws_row;
        term_size.width_pixels = w.ws_xpixel;
        term_size.height_pixels = w.ws_ypixel;
    }
    if (term_size.width_cells <= 0)
        term_size.width_cells = -1;
    if (term_size.height_cells <= 0)
        term_size.height_cells = -1;

    /* If .ws_xpixel and .ws_ypixel are filled out, we can calculate
     * aspect information for the font used. Sixel-capable terminals
     * like mlterm set these fields, but most others do not. */

    if (term_size.width_pixels <= 0 || term_size.height_pixels <= 0) {
        term_size.width_pixels = -1;
        term_size.height_pixels = -1;
    }

    *term_size_out = term_size;
}

void print_cli_usage() {
    printf("-h, --help           -> Prints this message.\n"
           "-r, --raw            -> Outputs raw data separated by '||'\n"
           "-i, --no-img         -> Do not display the thumbnail.\n"
           "-p, --db-path <path> -> Changes the path to the database.\n"
           "                        By default, the program will look for 'facts.db'\n"
           "                        in the same directory as the executable.\n"
           "-t, --type <type>    -> The type of fact to be displayed.\n"
           "                        Options are: selected, births, deaths, events and holidays.\n"
           "                        Default is 'selected'.\n"
           "-d, --day <day>      -> Display a fact for a specific day.\n"
           "-m, --month <month>  -> Display a fact for a specific month.\n");
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

    TermSize term_size;
    get_term_size(&term_size);

    CmdOptions options = {
        .output_raw = 0,
        .render_image = 1,
        .db_path = resolve_db_path(),
        .fact_type = "selected",
        .day = tm_info->tm_mday,
        .month = tm_info->tm_mon + 1,

        .term_size = term_size,
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
        .text = NULL,
        .thumb = NULL,
        .t_width = 0,
        .t_height = 0,
        .day = day,
        .month = month,
        .year = 0,
    };

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        fact.text = strdup((const char *)sqlite3_column_text(stmt, 0));
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            fact.thumb = strdup((const char *)sqlite3_column_text(stmt, 1));
        }
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

uint8_t chafa_render_image(unsigned char *pixels, int img_width, int img_height, ChafaTermInfo *term_info, TermSize term_size, gint *width_cells_out, gint *height_cells_out) {
    // https://github.com/hpjansson/chafa/blob/caafc58b2348032bc57d8b5f1af24905a414cb52/examples/adaptive.c
    gfloat font_ratio = 0.5;
    gint cell_width = -1, cell_height = -1; // Size of each character cell in pixels.
    gint width_cells, height_cells;         // Size of output image in cells;

    if (term_size.width_cells > 0 && term_size.height_cells > 0 && term_size.width_pixels > 0 && term_size.height_pixels > 0) {
        cell_width = term_size.width_pixels / term_size.width_cells;
        cell_height = term_size.height_pixels / term_size.height_cells;
        font_ratio = (gdouble)cell_width / (gdouble)cell_height;
    }
    width_cells = MAX_IMAGE_WIDTH;
    height_cells = MAX_IMAGE_HEIGHT;

    chafa_calc_canvas_geometry(img_width, img_height, &width_cells, &height_cells, font_ratio, FALSE, FALSE);

    ChafaCanvasMode mode = chafa_term_info_get_best_canvas_mode(term_info);
    ChafaPixelMode pixel_mode = chafa_term_info_get_best_pixel_mode(term_info);
    // Don't render symbols. They work differently.
    if (pixel_mode == 0) {
        return 0;
    }
    ChafaPassthrough passthrough = chafa_term_info_get_is_pixel_passthrough_needed(term_info, pixel_mode) ? chafa_term_info_get_passthrough_type(term_info) : CHAFA_PASSTHROUGH_NONE;

    ChafaCanvasConfig *config = chafa_canvas_config_new();
    chafa_canvas_config_set_canvas_mode(config, mode);
    chafa_canvas_config_set_pixel_mode(config, pixel_mode);
    chafa_canvas_config_set_geometry(config, width_cells, height_cells);
    chafa_canvas_config_set_passthrough(config, passthrough);

    if (cell_width > 0 && cell_height > 0) {
        /* We know the pixel dimensions of each cell. Store it in the config. */
        chafa_canvas_config_set_cell_geometry(config, cell_width, cell_height);
    }
    ChafaCanvas *canvas = chafa_canvas_new(config);
    chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, pixels, img_width, img_height, img_width * 4);

    GString *printable = chafa_canvas_print(canvas, term_info);
    uint8_t rendered = 0;
    if (printable != NULL) {
        fwrite(printable->str, sizeof(char), printable->len, stdout);
        fputc('\n', stdout);
        g_string_free(printable, TRUE);

        *width_cells_out = width_cells;
        *height_cells_out = height_cells;

        rendered = 1;
    } else {
        fprintf(stderr, "Failed to render image file!\n");

        *width_cells_out = -1;
        *height_cells_out = -1;
    }
    chafa_canvas_unref(canvas);
    chafa_canvas_config_unref(config);

    return rendered;
}

uint8_t render_thumb(char *thumb, ChafaTermInfo *term_info, TermSize term_size, gint *width_cells_out, gint *height_cells_out) {
    if (MAX_IMAGE_WIDTH * 3 > term_size.width_cells || MAX_IMAGE_HEIGHT + 5 > term_size.height_cells)
        return 0;

    // Save temp file
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

    // Load image.
    int img_width, img_height, channel_c;
    unsigned char *pixels = stbi_load(image_temp_file, &img_width, &img_height, &channel_c, STBI_rgb_alpha);
    if (pixels == NULL) {
        fprintf(stderr, "Failed to load image file!\n");
        unlink(image_temp_file);
        return 0;
    }

    // Render image
    gint width_cells, height_cells;
    uint8_t rendered = chafa_render_image(pixels, img_width, img_height, term_info, term_size, &width_cells, &height_cells);

    *width_cells_out = width_cells;
    *height_cells_out = height_cells;

    stbi_image_free(pixels);
    unlink(image_temp_file);

    return rendered;
}

char *number_to_ordinal(int n) {
    int ld = n % 10;
    char *s = "th";

    if (1 <= ld && ld <= 3) {
        if (n % 100 < 10 || n % 100 > 20) {
            s = (ld == 1)   ? "st"
                : (ld == 2) ? "nd"
                            : "rd";
        }
    }
    return s;
}

size_t wrap_pages_by_words(cJSON *pages, DS_SB_StringBuffer **sb_out, size_t initial_col, TermSize term_size) {
    cJSON *page;
    size_t i = 0;
    size_t row_c = 0, lines = 0;

    cJSON_ArrayForEach(page, pages) {
        if (i == 0) {
            ds_sb_append(*sb_out, "See: ");
            lines = 1;
        }
        cJSON *t_title = cJSON_GetObjectItemCaseSensitive(page, "title");
        cJSON *url = cJSON_GetObjectItemCaseSensitive(page, "url");
        if (cJSON_IsString(t_title) && *t_title->valuestring &&
            cJSON_IsString(url) && *url->valuestring) {
            char *title = t_title->valuestring;
            strip_title(title);

            if (initial_col + 5 + row_c + strlen(title) + 3 >= term_size.width_cells) {
                ds_sb_append(*sb_out, "\n     ");
                row_c = 5;
                lines++;
            } else if (i > 0) {
                ds_sb_append(*sb_out, " • ");
                row_c += 3;
            }
            // Blue, italic, underlined hyperlink
            ds_sb_append(*sb_out, "\033[34;3;4m\e]8;;");
            ds_sb_append(*sb_out, url->valuestring);
            ds_sb_append(*sb_out, "\e\\");
            ds_sb_append(*sb_out, title);
            ds_sb_append(*sb_out, "\e]8;;\e\\\033[0m");
            row_c += strlen(title);
        }
        i++;
    }
    return lines;
}

void print_fact(Fact fact, uint8_t image_rendered, ChafaTermInfo *term_info, TermSize term_size, gint width_cells, gint height_cells) {
    gchar seq[CHAFA_TERM_SEQ_LENGTH_MAX * 2 + 1];

    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    if (image_rendered && width_cells > 0 && height_cells > 0) {
        int initial_col = width_cells + 2;

        // Save cursor position:
        printf("\033[s");
        // Move cursor up:
        printf("\033[%dA", height_cells);
        // Move cursor to the correct column:
        printf("\033[%dG", initial_col);

        DS_SB_StringBuffer *word_sb = ds_sb_create();
        size_t row_c = 0;
        size_t line_c = 3;

        printf("\033[90m %s %d%s, %d \033[42m\033[30m In history \033[0m\033[32m\033[0m",
               months[fact.month - 1],
               fact.day,
               number_to_ordinal(fact.day),
               fact.year);
        // Move cursor two rows down
        printf("\033[2B");
        // Move cursor right
        printf("\033[%dG", initial_col);

        for (size_t i = 0; i < strlen(fact.text) + 1; i++) {
            if (fact.text[i] == ' ' || fact.text[i] == 0) {
                if (word_sb->size > 0) {
                    if (initial_col + row_c + word_sb->size + 1 > term_size.width_cells) {
                        // Move cursor to the next row
                        printf("\033[1B");
                        // Move cursor right
                        printf("\033[%dG", initial_col + 1);
                        row_c = 0;
                        line_c++;
                    } else {
                        putchar(' ');
                    }
                    printf("%s", word_sb->data);
                    row_c += word_sb->size + 1;
                    ds_sb_clear(word_sb);
                }
            } else if (fact.text[i] != '\n') {
                char c[2];
                c[0] = fact.text[i];
                c[1] = '\0';
                ds_sb_append(word_sb, c);
            }
        }
        size_t spare_lines = height_cells - line_c;
        size_t pages_rc = 0;
        row_c = 0;

        ds_sb_clear(word_sb);

        if (cJSON_IsArray(fact.pages)) {
            pages_rc = wrap_pages_by_words(fact.pages, &word_sb, initial_col, term_size);
        }

        size_t lines_to_jump = 1;
        if (pages_rc <= spare_lines - 1) {
            // Align pages bottom
            lines_to_jump += spare_lines - pages_rc;
        } else {
            // Jump 1 line and print pages
            lines_to_jump += 1;
        }
        if (word_sb != NULL) {
            // Move cursor to n rows down
            printf("\033[%zuB", lines_to_jump);
            // Move cursor right
            printf("\033[%dG", initial_col + 1);
            line_c += lines_to_jump;
            for (size_t i = 0; i < word_sb->size; i++) {
                if (word_sb->data[i] == '\n') {
                    // Move cursor to the next row
                    printf("\033[1B");
                    // Move cursor right
                    printf("\033[%dG", initial_col + 1);
                    line_c++;
                } else {
                    putchar(word_sb->data[i]);
                }
            }
        }
        // Restore cursor position:
        printf("\033[u");
        if (line_c > height_cells)
            printf("\033[%zuB", line_c - height_cells);
        printf("\033[0G\n");

        ds_sb_free(word_sb);

    } else {
        printf("\033[90m %s %d%s, %d \033[42m\033[30m In history \033[0m\033[32m\033[0m\n\n",
               months[fact.month - 1],
               fact.day,
               number_to_ordinal(fact.day),
               fact.year);
        printf("%s\n", fact.text);

        DS_SB_StringBuffer *sb = ds_sb_create();
        if (cJSON_IsArray(fact.pages)) {
            wrap_pages_by_words(fact.pages, &sb, 0, term_size);
        }
        if (sb != NULL && sb->size > 0)
            printf("\n%s\n", sb->data);

        ds_sb_free(sb);
    }
}

int main(int argc, char **argv) {
    CmdOptions options = parse_cmdline(argc, argv);

    Fact fact = query_data(options, options.fact_type, options.day, options.month);

    if (options.output_raw)
        print_raw(fact);
    else {
        gchar **envp = g_get_environ();
        ChafaTermInfo *term_info = chafa_term_db_detect(chafa_term_db_get_default(), envp);

        gint width_cells, height_cells;
        uint8_t image_rendered = 0;

        if (options.render_image && fact.thumb && strlen(fact.thumb) > 0) {
            image_rendered = render_thumb(fact.thumb, term_info, options.term_size, &width_cells, &height_cells);
        }
        print_fact(fact, image_rendered, term_info, options.term_size, width_cells, height_cells);

        chafa_term_info_unref(term_info);
        g_strfreev(envp);
    }
    free(fact.text);
    free(fact.thumb);
    cJSON_Delete(fact.pages);

    return 0;
}

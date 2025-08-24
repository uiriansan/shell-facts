#include "cjson/cJSON.h"
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

const char *DB_PATH = "/facts.db";

typedef struct {
  char *text;
  unsigned int year;
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
    fprintf(stderr, "[ERROR]: Could not resolve DB_PATH.\n");
    exit(1);
  }
}

void parse_data(Fact fact) {}

void print_fact(Fact fact) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

  if (cJSON_IsArray(fact.pages)) {
    cJSON *first_page = cJSON_GetArrayItem(fact.pages, 0);
    cJSON *thumb = cJSON_GetObjectItemCaseSensitive(first_page, "thumb");
    if (cJSON_IsString(thumb) && *thumb->valuestring) {
      printf("Thumb: %s\n", thumb->valuestring);
    }
  }

  printf("Text: %s\nYear: %d\n", fact.text, fact.year);
  printf("Term size: %dx%d\n", w.ws_row, w.ws_col);
}

int main(int argc, char **argv) {
  char *db_path = resolve_db_path();

  sqlite3 *db;
  int rc = sqlite3_open(db_path, &db);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[ERROR]: Failed to open database: %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  sqlite3_stmt *stmt;
  const char *sql = "SELECT text, year, pages FROM Facts WHERE type LIKE "
                    "'selected' AND day LIKE ? AND month LIKE "
                    "? ORDER BY RANDOM() LIMIT 1;";

  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[ERROR]: Failed to prepare SQL query: %s\n",
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  sqlite3_bind_int(stmt, 1, tm_info->tm_mday);
  sqlite3_bind_int(stmt, 2, tm_info->tm_mon + 1);

  Fact fact = {.text = "", .year = 0};

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    fact.text = strdup((const char *)sqlite3_column_text(stmt, 0));
    fact.year = sqlite3_column_int(stmt, 1);
    fact.pages =
        cJSON_Parse(strdup((const char *)sqlite3_column_text(stmt, 2)));
    if (fact.pages == NULL) {
      fprintf(stderr, "[ERROR]: Failed to parse pages: %s\n",
              cJSON_GetErrorPtr());
    }
  } else {
    fprintf(stderr, "[ERROR]: Failed to retrieve data.\n");
    sqlite3_close(db);
    return 1;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  print_fact(fact);

  free(fact.text);
  cJSON_Delete(fact.pages);
  return 0;
}

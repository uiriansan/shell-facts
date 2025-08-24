#include "cjson/cJSON.h"
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

const char *DB_PATH = "/facts.db";

typedef struct {
  char *text;
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

void print_fact(Fact fact) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

  if (cJSON_IsArray(fact.pages)) {
    cJSON *page, *thumb;
    size_t i = 0;
    cJSON_ArrayForEach(page, fact.pages) {
      if (i == 0) {
        thumb = cJSON_GetObjectItemCaseSensitive(page, "thumb");
      }

      cJSON *title = cJSON_GetObjectItemCaseSensitive(page, "title");
      cJSON *url = cJSON_GetObjectItemCaseSensitive(page, "url");
      if (cJSON_IsString(title) && *title->valuestring && cJSON_IsString(url) &&
          *url->valuestring) {
        // Blue, italic, underlined hyperlink
        printf("\033[34;3;4m\e]8;;%s\e\\%s\e]8;;\e\\\n\033[0m",
               url->valuestring, title->valuestring);
      }
      i++;
    }
    if (cJSON_IsString(thumb) && *thumb->valuestring) {
      printf("Thumb: %s\n", thumb->valuestring);
    }
  }

  printf("Text: %s\nYear: %d%s\n", fact.text, abs(fact.year),
         fact.year < 0 ? " BC" : "");
  printf("Term size: %dx%d\n", w.ws_row, w.ws_col);
}

int main(int argc, char **argv) {
  char *db_path = resolve_db_path();
  if (db_path == NULL) {
    fprintf(stderr, "[ERROR]: Could not resolve DB_PATH.\n");
    return 1;
  }

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

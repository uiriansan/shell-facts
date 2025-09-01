#ifndef DS_SB_H_
#define DS_SB_H_

/*
 * https://briandouglas.ie/string-buffer-c/
 */

#include <stddef.h>
#include <string.h>

#define INITIAL_CAPACITY 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DS_SB_StringBuffer DS_SB_StringBuffer;
static inline DS_SB_StringBuffer *ds_sb_create();
static inline void ds_sb_free(DS_SB_StringBuffer *sb);
static inline void ds_sb_append(DS_SB_StringBuffer *sb, char *str);
static inline void ds_sb_clear(DS_SB_StringBuffer *sb);

#ifdef __cplusplus
}
#endif

#ifdef DS_SB_IMPLEMENTATION
#include <stdint.h>
#include <stdlib.h>

struct DS_SB_StringBuffer {
    char *data;
    size_t size;
    size_t capacity;
};

static inline DS_SB_StringBuffer *ds_sb_create() {
    DS_SB_StringBuffer *sb = (DS_SB_StringBuffer *)malloc(sizeof(DS_SB_StringBuffer));
    if (sb == NULL)
        return NULL;

    sb->size = 0;
    sb->capacity = 0;
    sb->data = NULL;
    return sb;
}

static inline void ds_sb_free(DS_SB_StringBuffer *sb) {
    if (sb == NULL)
        return;
    free(sb->data);
    free(sb);
}

static inline uint8_t ds_sb_ensure_capacity(DS_SB_StringBuffer *sb, size_t required) {
    if (required <= sb->capacity)
        return 1;

    size_t new_capacity = sb->capacity ? sb->capacity : INITIAL_CAPACITY;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    char *new_data = (char *)realloc(sb->data, new_capacity);
    if (new_data == NULL)
        return 0;

    sb->data = new_data;
    sb->capacity = new_capacity;
    return 1;
}

static inline void ds_sb_append(DS_SB_StringBuffer *sb, char *str) {
    if (sb == NULL || str == NULL)
        return;

    if (!ds_sb_ensure_capacity(sb, sb->size + strlen(str) + 1))
        return;

    memcpy(sb->data + sb->size, str, strlen(str) + 1);
    sb->size += strlen(str);
}

static inline void ds_sb_clear(DS_SB_StringBuffer *sb) {
    if (sb == NULL || sb->data == NULL)
        return;

    sb->data[0] = '\0';
    sb->size = 0;
}
#endif // DS_SB_IMPLEMENTATION
#endif // DS_SB_H_

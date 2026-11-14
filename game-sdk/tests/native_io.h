/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef GAME_TEST_NATIVE_IO_H
#define GAME_TEST_NATIVE_IO_H
#define OSOS_GAME_NATIVE_H
#include "osos_game_abi.h"

void osos_directory_init(struct osos_directory_iterator *, const char *, int);
int osos_directory_next(struct osos_directory_iterator *,
                        struct osos_native_path_string *, uint8_t *);
void osos_directory_destroy(struct osos_directory_iterator *);
void osos_path_string_init(struct osos_native_path_string *);
const char *osos_path_string_text(struct osos_native_path_string *);
void osos_path_string_destroy(struct osos_native_path_string *);
int osos_file_open(const struct osos_path *, void **);
int osos_file_size(void *, uint32_t *);
int osos_file_read(void *, void *, uint32_t, uint32_t *);
int osos_file_seek(void *, int64_t);
void osos_file_close(void *);
#endif

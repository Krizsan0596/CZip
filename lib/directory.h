#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "data_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * Recursively walks the directory and serializes every entry.
 * If out_data is NULL, it only calculates and returns the total serialized size.
 * If out_data is not NULL, it serializes entries into the buffer.
 */
int64_t archive_directory(char *path, int *archive_size, int64_t *data_size, uint8_t *out_data);

/*
 * Serializes an archived directory element into a buffer.
 * If out_data is NULL, it only calculates and returns the item's serialized size.
 */
int64_t serialize_item(Directory_item *item, uint8_t *out_data);

/*
 * Extracts the archived directory to the given path.
 */
int extract_directory(char *path, Directory_item *item, bool force, bool no_preserve_perms);

/*
 * Reconstructs the archive array from the serialized buffer.
 */
int64_t deserialize_item(Directory_item *item, const uint8_t *in_data, int64_t remaining_size);

/*
 * Prepares a directory for compression by serializing it into a memory-mapped temporary file.
 * Returns the file descriptor on success or a negative error code.
 */
int prepare_directory(char *input_file, int *directory_size, uint64_t *total_size);

/*
 * Handles directory processing for extraction from a memory-mapped buffer.
 */
int restore_directory(const uint8_t *temp_data, int64_t temp_size, char *output_file, bool force, bool no_preserve_perms);

#endif /* DIRECTORY_H */

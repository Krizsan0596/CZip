#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include "data_types.h"

int decompress(Compressed_file *compressed, uint8_t *raw);
// Output pointer arguments must be valid addresses; the function allocates and fills the data.
int run_decompression(Arguments args, uint8_t **raw_data, long *raw_size, bool *is_directory, char **original_name);

#endif

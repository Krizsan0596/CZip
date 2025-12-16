#ifndef FILE_H
#define FILE_H
#include "data_types.h"
#include <stdio.h>
#include <stdbool.h>

int read_raw(char file_name[], const uint8_t** data);
int read_from_file(FILE *f, uint8_t** data);
int write_raw(char file_name[], uint8_t** data, long file_size, bool overwrite);
int read_compressed(char file_name[], Compressed_file *compressed, const uint8_t **mmap_ptr);
int write_compressed(Compressed_file *compressed, bool overwrite); 
long get_file_size(FILE* f);
const char* get_unit(size_t *bytes);

#endif

#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "data_types.h"
#include <stdio.h>

long archive_directory(char *path, int *archive_size, long *data_size, FILE *f);
long serialize_item(Directory_item *item, FILE *f);
long deserialize_item(Directory_item *item, FILE *f);
int extract_directory(char *path, Directory_item *item, bool force, bool no_preserve_perms);
FILE* prepare_directory(char *input_file, int *directory_size);
int restore_directory(FILE *temp_file, char *output_file, bool force, bool no_preserve_perms);

#endif // DIRECTORY_H

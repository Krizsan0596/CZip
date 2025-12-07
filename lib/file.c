#include "file.h"
#include "data_types.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "debugmalloc.h"



/*
 * Determines the length of an open file.
 * Returns the size on success or -1 on failure.
 */
long get_file_size(FILE *f){
    long current = ftell(f);
    if (fseek(f, 0, SEEK_END) != 0) return FILE_READ_ERROR;
    long size = ftell(f);
    fseek(f, 0, current);
    return size;
} 

/*
 * Converts the byte count to a larger unit while updating the supplied size.
 * Returns the abbreviation of the chosen unit.
 */
const char* get_unit(int *bytes) {
    if (*bytes < 1024) return "B";
    *bytes /= 1024;
    if (*bytes < 1024) return "KB";
    *bytes /= 1024;
    if (*bytes < 1024) return "MB";
    *bytes /= 1024;
    return "GB";
}

/*
 * Reads the file into memory; the caller supplies the pointer.
 * Returns the number of bytes read on success or a negative code on error.
 */
int read_raw(char file_name[], char** data){
    FILE* f;
    f = fopen(file_name, "rb");
    if (f == NULL) return FILE_READ_ERROR; // Not exists.
    long file_size = get_file_size(f);
    if (file_size == 0) {
        *data = NULL;
        fclose(f);
        return EMPTY_FILE;
    }
    *data = (char*)malloc(file_size);
    if (*data == NULL) {
        fclose(f);
        return MALLOC_ERROR;
    }
    size_t read_size = fread(*data, sizeof(char), file_size, f);
    if (read_size != file_size) {
        free(*data);
        *data = NULL;
        fclose(f);
        return FILE_READ_ERROR;
    }
    fclose(f);
    return read_size;
}

/*
 * Writes the given buffer to disk; prompts before overwriting when overwrite is false.
 * Verifies the entire file was written; returns negative error codes on failure.
 */
int write_raw(char *file_name, char *data, long file_size, bool overwrite){
    FILE* f;
    f = fopen(file_name, "r");
    if (f != NULL) { 
        if (!overwrite) {
            fclose(f);
            printf("The file (%s) exists. Overwrite? [Y/n]>", file_name);
            char input;
            if (scanf(" %c", &input) != 1) return SCANF_FAILED; 
            if (tolower(input) != 'y') return NO_OVERWRITE;
        }
        else fclose(f);
    }
    f = fopen(file_name, "wb");
    if (f == NULL) return FILE_WRITE_ERROR;
    long written_size = fwrite(data, sizeof(char), file_size, f);
    fclose(f);
    if (file_size != written_size) return FILE_WRITE_ERROR;
    return written_size;
}

/*
 * Reads the stored Compressed_file format and verifies the required data is present.
 * Allocates all necessary buffers and populates the Compressed_file structure written by write_compressed.
 */
int read_compressed(char file_name[], Compressed_file *compressed){
    int ret = SUCCESS;
    FILE* f = fopen(file_name, "rb");
    if (f == NULL) {
        return FILE_READ_ERROR; 
    }

    compressed->original_file = NULL;
    compressed->huffman_tree = NULL;
    compressed->compressed_data = NULL;
    compressed->file_name = NULL;

    while (true) {
        if (fread(compressed->magic, sizeof(char), sizeof(magic), f) != sizeof(magic)) {
            ret = FILE_READ_ERROR;
            break;
        }

        if (memcmp(compressed->magic, magic, sizeof(magic)) != 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        if (fread(&compressed->is_dir, sizeof(bool), 1, f) != 1) {
            ret = FILE_READ_ERROR;
            break;
        }

        if (fread(&compressed->original_size, sizeof(long), 1, f) != 1) {
            ret = FILE_READ_ERROR;
            break;
        }

        long name_len = 0;
        if (fread(&name_len, sizeof(long), 1, f) != 1) {
            ret = FILE_READ_ERROR;
            break;
        }
        if (name_len < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        compressed->original_file = (char*)malloc(name_len + 1);
        if (compressed->original_file == NULL) {
            ret = MALLOC_ERROR;
            break;
        }
        if ((long)fread(compressed->original_file, sizeof(char), name_len, f) != name_len) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->original_file[name_len] = '\0';

        if (fread(&compressed->tree_size, sizeof(long), 1, f) != 1) {
            ret = FILE_READ_ERROR;
            break;
        }
        if (compressed->tree_size < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        compressed->huffman_tree = (Node*)malloc(compressed->tree_size);
        if (compressed->huffman_tree == NULL) {
            ret = MALLOC_ERROR;
            break;
        }
        if ((long)fread(compressed->huffman_tree, sizeof(char), compressed->tree_size, f) != compressed->tree_size) {
            ret = FILE_READ_ERROR;
            break;
        }

        if (fread(&compressed->data_size, sizeof(long), 1, f) != 1) {
            ret = FILE_READ_ERROR;
            break;
        }
        if (compressed->data_size < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        long compressed_bytes = (long)ceil((double)compressed->data_size / 8.0);
        compressed->compressed_data = (char*)malloc(compressed_bytes * sizeof(char));
        if (compressed->compressed_data == NULL) {
            ret = MALLOC_ERROR;
            break;
        }
        if ((long)fread(compressed->compressed_data, sizeof(char), compressed_bytes, f) != compressed_bytes) {
            ret = FILE_READ_ERROR;
            break;
        }

        compressed->file_name = strdup(file_name);
        if (compressed->file_name == NULL) {
            ret = MALLOC_ERROR;
            break;
        }

        break;
    }

    fclose(f);

    if (ret != SUCCESS) {
        free(compressed->original_file);
        free(compressed->huffman_tree);
        free(compressed->compressed_data);
        free(compressed->file_name);
        compressed->original_file = NULL;
        compressed->huffman_tree = NULL;
        compressed->compressed_data = NULL;
        compressed->file_name = NULL;
    }

    return ret;
}
/*
 * Serializes the provided structure and writes it to the given file.
 * The actual write is performed by write_raw.
 */
int write_compressed(Compressed_file *compressed, bool overwrite) {
    long name_len = strlen(compressed->original_file);
    long file_size = (sizeof(char) * 4) + sizeof(bool) + sizeof(long) + sizeof(long) + name_len * sizeof(char) + sizeof(long) + compressed->tree_size + sizeof(long) + (compressed->data_size + 7) / 8;
    char *data = malloc(file_size);
    if (data == NULL) {
        return MALLOC_ERROR;
    }
    char *current = &data[0];
    for (int i = 0; i < 4; i++) {
        data[i] = magic[i];
    }
    current += 4;
    memcpy(current, &compressed->is_dir, sizeof(bool));
    current += sizeof(bool);
    memcpy(current, &compressed->original_size, sizeof(long));
    current += sizeof(long);
    memcpy(current, &name_len, sizeof(long));
    current += sizeof(long);
    memcpy(current, compressed->original_file, name_len);
    current += name_len;
    memcpy(current, &compressed->tree_size, sizeof(long));
    current += sizeof(long);
    memcpy(current, compressed->huffman_tree, compressed->tree_size);
    current += compressed->tree_size;
    memcpy(current, &compressed->data_size, sizeof(long));
    current += sizeof(long);
    memcpy(current, compressed->compressed_data, (compressed->data_size + 7) / 8);
    int res = write_raw(compressed->file_name, data, file_size, overwrite);
    free(data);
    return res;
}

#include "file.h"
#include "data_types.h"
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
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
int read_raw(char file_name[], const char** data){
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) return FILE_READ_ERROR;
    struct stat st;
    if (fstat(fd, &st) == -1) return FILE_READ_ERROR;
    long file_size = st.st_size;
    if (file_size == 0) return EMPTY_FILE;
    void *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) return FILE_READ_ERROR;
    *data = map;
    return file_size;
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
 * Allocates buffers for strings and tree, but points compressed_data directly into the mmap.
 * Caller must munmap using the returned mmap_ptr and size (returned as function result).
 */
int read_compressed(char file_name[], Compressed_file *compressed, const char **mmap_ptr){
    int ret = SUCCESS;
    const char* data = NULL;
    int file_size = read_raw(file_name, &data);
    
    if (file_size < 0) {
        return file_size;
    }

    compressed->original_file = NULL;
    compressed->huffman_tree = NULL;
    compressed->compressed_data = NULL;
    compressed->file_name = NULL;

    const char* current = data;
    const char* end = data + file_size;

    while (true) {
        if (current + sizeof(magic) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        memcpy(compressed->magic, current, sizeof(magic));
        current += sizeof(magic);

        if (memcmp(compressed->magic, magic, sizeof(magic)) != 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        if (current + sizeof(bool) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->is_dir = *(bool*)current;
        current += sizeof(bool);

        if (current + sizeof(long) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->original_size = *(long*)current;
        current += sizeof(long);

        long name_len = 0;
        if (current + sizeof(long) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        name_len = *(long*)current;
        current += sizeof(long);
        
        if (name_len < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        if (current + name_len > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->original_file = (char*)malloc(name_len + 1);
        if (compressed->original_file == NULL) {
            ret = MALLOC_ERROR;
            break;
        }
        memcpy(compressed->original_file, current, name_len);
        compressed->original_file[name_len] = '\0';
        current += name_len;

        if (current + sizeof(long) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->tree_size = *(long*)current;
        current += sizeof(long);
        
        if (compressed->tree_size < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        if (current + compressed->tree_size > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->huffman_tree = (Node*)current;
        current += compressed->tree_size;

        if (current + sizeof(long) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->data_size = *(long*)current;
        current += sizeof(long);
        
        if (compressed->data_size < 0) {
            ret = FILE_MAGIC_ERROR;
            break;
        }

        long compressed_bytes = (long)ceil((double)compressed->data_size / 8.0);
        if (current + compressed_bytes > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->compressed_data = (char*)current;

        compressed->file_name = strdup(file_name);
        if (compressed->file_name == NULL) {
            ret = MALLOC_ERROR;
            break;
        }

        break;
    }

    if (ret != SUCCESS) {
        munmap((void*)data, file_size);
        free(compressed->original_file);
        free(compressed->file_name);
        compressed->original_file = NULL;
        compressed->huffman_tree = NULL;
        compressed->compressed_data = NULL;
        compressed->file_name = NULL;
        return ret;
    }

    *mmap_ptr = data;
    return file_size;
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

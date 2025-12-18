#include "file.h"
#include "data_types.h"
#include "compatibility.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>



/*
 * Determines the length of an open file.
 * Returns the size on success or FILE_READ_ERROR on failure.
 */
long get_file_size(FILE *file){
    long current = ftell(file);
    if (fseek(file, 0, SEEK_END) != 0) return FILE_READ_ERROR;
    long size = ftell(file);
    if (fseek(file, current, SEEK_SET) != 0) return FILE_READ_ERROR;
    return size;
} 

/*
 * Converts the byte count to a larger unit while updating the supplied size.
 * Returns the abbreviation of the chosen unit.
 */
const char* get_unit(size_t *bytes) {
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
int read_raw(char file_name[], const uint8_t** data){
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) return FILE_READ_ERROR;
    struct stat st;
    if (fstat(fd, &st) == -1) { close(fd); return FILE_READ_ERROR; }
    long file_size = st.st_size;
    if (file_size == 0) { close(fd); return EMPTY_FILE; }
    void *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return FILE_READ_ERROR; }
    close(fd);
    *data = map;
    return file_size;
}

/*
 * Reads data from an open FILE* into an allocated buffer.
 * Returns the number of bytes read on success or a negative code on error.
 * Caller must free the returned buffer.
 */
int read_from_file(FILE *file, uint8_t** data) {
    if (file == NULL) return FILE_READ_ERROR;
    
    long file_size = get_file_size(file);
    if (file_size < 0) return FILE_READ_ERROR;
    if (file_size == 0) return EMPTY_FILE;
    
    rewind(file);
    
    *data = malloc(file_size);
    if (*data == NULL) return MALLOC_ERROR;
    
    size_t bytes_read = fread(*data, 1, file_size, file);
    if (bytes_read != (size_t)file_size) {
        free(*data);
        *data = NULL;
        return FILE_READ_ERROR;
    }
    
    return file_size;
}

/*
 * Creates a file and memory maps it for writing.
 * Returns the file size on success or negative error codes on failure.
 * Caller must munmap the returned pointer.
 */
int write_raw(char *file_name, uint8_t **data, long file_size, bool overwrite){
    int fd = -1;
    void *map = NULL;
    int ret = SUCCESS;
    
    while (true) {
        if (!overwrite && access(file_name, F_OK) == 0) {
            printf("The file (%s) exists. Overwrite? [Y/n]>", file_name);
            fflush(stdout);
            int input = fgetc(stdin);
            if (input == EOF) {
                ret = SCANF_FAILED;
                break;
            }
            if (tolower(input) != 'y') {
                ret = NO_OVERWRITE;
                break;
            }
        }
        
        fd = open(file_name, O_CREAT | O_TRUNC | O_RDWR, FILE_MODE);
        if (fd == -1) {
            ret = FILE_WRITE_ERROR;
            break;
        }
        
        if (ftruncate(fd, file_size) == -1) {
            ret = FILE_WRITE_ERROR;
            break;
        }
        
        map = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            ret = FILE_WRITE_ERROR;
            break;
        }
        
        close(fd);
        *data = map;
        return file_size;
    }
    
    if (fd != -1) {
        close(fd);
    }
    
    return ret;
}

/*
 * Reads the stored Compressed_file format and verifies the required data is present.
 * File content is mmapped; the Huffman tree and compressed data pointers reference that mapping,
 * while file names are duplicated onto the heap.
 * Caller must munmap using the returned mmap_ptr and size (returned as function result).
 */
int read_compressed(char file_name[], Compressed_file *compressed, const uint8_t **mmap_ptr){
    int ret = SUCCESS;
    const uint8_t* data = NULL;
    long file_size = read_raw(file_name, &data);
    
    if (file_size < 0) {
        return file_size;
    }

    compressed->original_file = NULL;
    compressed->huffman_tree = NULL;
    compressed->compressed_data = NULL;
    compressed->file_name = NULL;

    const uint8_t* current = data;
    const uint8_t* end = data + file_size;

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

        if (current + sizeof(size_t) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->original_size = *(size_t*)current;
        current += sizeof(size_t);

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
        compressed->original_file = malloc(name_len + 1);
        if (compressed->original_file == NULL) {
            ret = MALLOC_ERROR;
            break;
        }
        memcpy(compressed->original_file, current, name_len);
        compressed->original_file[name_len] = '\0';
        current += name_len;

        if (current + sizeof(uint64_t) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->tree_size = *(uint64_t*)current;
        current += sizeof(uint64_t);

        if (current + compressed->tree_size > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->huffman_tree = (Node*)current;
        current += compressed->tree_size;

        if (current + sizeof(uint64_t) > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->data_size = *(uint64_t*)current;
        current += sizeof(uint64_t);

        size_t compressed_bytes = (size_t)ceil((double)compressed->data_size / 8.0);
        if (current + compressed_bytes > end) {
            ret = FILE_READ_ERROR;
            break;
        }
        compressed->compressed_data = (uint8_t*)current;

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
 * Serializes the provided structure and writes it directly into a newly mmapped output file.
 * Prompts for overwrite (unless forced) and persists the data with msync before closing.
 */
int write_compressed(Compressed_file *compressed, bool overwrite) {
    size_t name_len = strlen(compressed->original_file);
    long file_size = (sizeof(char) * 4) + sizeof(bool) + sizeof(size_t) + sizeof(long) +
                     name_len * sizeof(char) + sizeof(uint64_t) + compressed->tree_size +
                     sizeof(uint64_t) + (compressed->data_size + 7) / 8;

    uint8_t *map = NULL;
    int ret = write_raw(compressed->file_name, &map, file_size, overwrite);
    if (ret < 0) {
        return ret;
    }

    uint8_t *data = map;
    for (int i = 0; i < 4; i++) {
        data[i] = magic[i];
    }
    data += sizeof(char) * 4;
    memcpy(data, &compressed->is_dir, sizeof(bool));
    data += sizeof(bool);
    memcpy(data, &compressed->original_size, sizeof(size_t));
    data += sizeof(size_t);
    memcpy(data, &name_len, sizeof(long));
    data += sizeof(long);
    memcpy(data, compressed->original_file, name_len);
    data += name_len;
    memcpy(data, &compressed->tree_size, sizeof(uint64_t));
    data += sizeof(uint64_t);
    memcpy(data, compressed->huffman_tree, compressed->tree_size);
    data += compressed->tree_size;
    memcpy(data, &compressed->data_size, sizeof(uint64_t));
    data += sizeof(uint64_t);
    memcpy(data, compressed->compressed_data, (compressed->data_size + 7) / 8);

    if (msync(map, file_size, MS_SYNC) == -1) {
        ret = FILE_WRITE_ERROR;
    } else {
        ret = file_size;
    }

    munmap(map, file_size);
    return ret;
}

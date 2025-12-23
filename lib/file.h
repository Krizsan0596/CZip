#ifndef FILE_H
#define FILE_H
#include "data_types.h"
#include <stdio.h>
#include <stdbool.h>

/*
 * Reads the file into memory; the caller supplies the pointer.
 * Returns the number of bytes read on success or a negative code on error.
 */
int64_t read_raw(char file_name[], const uint8_t** data);

/*
 * Reads from a file descriptor into memory (mmap).
 * Returns the number of bytes read on success or a negative code on error.
 */
int64_t read_raw_from_fd(int fd, const uint8_t** data);

/*
 * Creates a file and memory maps it for writing.
 * Returns the file size on success or negative error codes on failure.
 * Caller must munmap the returned pointer.
 */
int64_t write_raw(char *file_name, uint8_t **data, uint64_t file_size, bool overwrite);

/*
 * Memory maps an existing file descriptor for writing.
 * Returns the file size on success or negative error codes on failure.
 * Caller must munmap the returned pointer.
 */
int64_t write_raw_to_fd(int fd, uint8_t **data, uint64_t file_size);

/*
 * Reads the stored Compressed_file format and verifies the required data is present.
 * File content is mmapped; the Huffman tree and compressed data pointers reference that mapping,
 * while file names are duplicated onto the heap.
 * Caller must munmap using the returned mmap_ptr and size (returned as function result).
 */
int64_t read_compressed(char file_name[], Compressed_file *compressed, const uint8_t **mmap_ptr);

/*
 * Serializes the provided structure and writes it directly into a newly mmapped output file.
 * Prompts for overwrite (unless forced) and persists the data with msync before closing.
 */
int64_t write_compressed(Compressed_file *compressed, bool overwrite); 

/*
 * Converts the byte count to a larger unit while updating the supplied size.
 * Returns the abbreviation of the chosen unit.
 */
const char* get_unit(size_t *bytes);

#endif /* FILE_H */
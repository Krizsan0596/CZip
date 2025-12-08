#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "../lib/file.h"
#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"

void test_file_io() {
    // 1. Setup
    Compressed_file original_data;
    memcpy(original_data.magic, magic, sizeof(original_data.magic));
    original_data.is_dir = false;
    original_data.original_size = 123;
    original_data.original_file = strdup("test.txt");
    original_data.tree_size = 45;
    original_data.huffman_tree = malloc(original_data.tree_size);
    memset(original_data.huffman_tree, 'A', original_data.tree_size);
    original_data.data_size = 80;
    long compressed_bytes = (long)ceil((double)original_data.data_size / 8.0);
    original_data.compressed_data = malloc(compressed_bytes);
    memset(original_data.compressed_data, 'B', compressed_bytes);
    original_data.file_name = strdup("test.huf");

    // 2. Action
    assert(write_compressed(&original_data, true) >= 0);

    // 3. Verification
    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("test.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(original_data.original_size == read_data.original_size);
    assert(strcmp(original_data.original_file, read_data.original_file) == 0);
    assert(original_data.tree_size == read_data.tree_size);
    assert(memcmp(original_data.huffman_tree, read_data.huffman_tree, original_data.tree_size) == 0);
    assert(original_data.data_size == read_data.data_size);
    assert(memcmp(original_data.compressed_data, read_data.compressed_data, compressed_bytes) == 0);

    // 4. Teardown
    munmap((void*)mmap_ptr, mmap_size);
    free(original_data.original_file);
    free(original_data.huffman_tree);
    free(original_data.compressed_data);
    free(original_data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("test.huf");

    printf("test_file_io passed.\n");
}

/* ===== EDGE CASE TESTS ===== */

void test_file_io_with_empty_filename() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 50;
    data.original_file = strdup("");  // Empty filename
    data.tree_size = 20;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'X', data.tree_size);
    data.data_size = 30;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    memset(data.compressed_data, 'Y', compressed_bytes);
    data.file_name = strdup("empty_name.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("empty_name.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.original_size == read_data.original_size);
    assert(strcmp(data.original_file, read_data.original_file) == 0);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("empty_name.huf");
    printf("test_file_io_with_empty_filename passed.\n");
}

void test_file_io_with_long_filename() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 75;
    
    // Create very long filename (200 chars)
    char long_name[256];
    for (int i = 0; i < 200; i++) {
        long_name[i] = 'a' + (i % 26);
    }
    long_name[200] = '\0';
    data.original_file = strdup(long_name);
    
    data.tree_size = 30;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'L', data.tree_size);
    data.data_size = 40;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    memset(data.compressed_data, 'M', compressed_bytes);
    data.file_name = strdup("long_name.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("long_name.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.original_size == read_data.original_size);
    assert(strcmp(data.original_file, read_data.original_file) == 0);
    assert(data.tree_size == read_data.tree_size);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("long_name.huf");
    printf("test_file_io_with_long_filename passed.\n");
}

void test_file_io_with_zero_size_data() {
    // Test writing zero-size data
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 0;
    data.original_file = strdup("zero_size.txt");
    data.tree_size = 10;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'Z', data.tree_size);
    data.data_size = 0;
    data.compressed_data = NULL;
    data.file_name = strdup("zero_size.huf");

    int write_result = write_compressed(&data, true);
    assert(write_result >= 0);

    // Reading back zero-size data may not be supported
    // Just verify write succeeded
    struct stat st;
    assert(stat("zero_size.huf", &st) == 0);

    free(data.original_file);
    free(data.huffman_tree);
    free(data.file_name);

    remove("zero_size.huf");
    printf("test_file_io_with_zero_size_data passed.\n");
}

void test_file_io_with_large_tree() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 500;
    data.original_file = strdup("large_tree.txt");
    
    // Large tree (10KB)
    data.tree_size = 10240;
    data.huffman_tree = malloc(data.tree_size);
    for (long i = 0; i < data.tree_size; i++) {
        ((char*)data.huffman_tree)[i] = (char)(i % 256);
    }
    
    data.data_size = 200;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    memset(data.compressed_data, 'T', compressed_bytes);
    data.file_name = strdup("large_tree.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("large_tree.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.original_size == read_data.original_size);
    assert(data.tree_size == read_data.tree_size);
    assert(memcmp(data.huffman_tree, read_data.huffman_tree, data.tree_size) == 0);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("large_tree.huf");
    printf("test_file_io_with_large_tree passed.\n");
}

void test_file_io_with_binary_data() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 256;
    data.original_file = strdup("binary.dat");
    data.tree_size = 50;
    data.huffman_tree = malloc(data.tree_size);
    
    // Fill with all byte values
    for (int i = 0; i < data.tree_size; i++) {
        ((char*)data.huffman_tree)[i] = (char)(i % 256);
    }
    
    data.data_size = 2048;  // 256 bytes
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    
    // Fill with all byte values including nulls
    for (long i = 0; i < compressed_bytes; i++) {
        ((char*)data.compressed_data)[i] = (char)(i % 256);
    }
    
    data.file_name = strdup("binary.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("binary.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.original_size == read_data.original_size);
    assert(data.tree_size == read_data.tree_size);
    assert(data.data_size == read_data.data_size);
    assert(memcmp(data.huffman_tree, read_data.huffman_tree, data.tree_size) == 0);
    assert(memcmp(data.compressed_data, read_data.compressed_data, compressed_bytes) == 0);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("binary.huf");
    printf("test_file_io_with_binary_data passed.\n");
}

void test_file_io_directory_flag() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = true;  // Mark as directory
    data.original_size = 100;
    data.original_file = strdup("test_dir");
    data.tree_size = 25;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'D', data.tree_size);
    data.data_size = 50;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    memset(data.compressed_data, 'E', compressed_bytes);
    data.file_name = strdup("directory.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("directory.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.is_dir == read_data.is_dir);
    assert(read_data.is_dir == true);
    assert(data.original_size == read_data.original_size);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("directory.huf");
    printf("test_file_io_directory_flag passed.\n");
}

void test_file_io_special_chars_in_original_filename() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 80;
    data.original_file = strdup("file-with_special.chars$123.txt");
    data.tree_size = 35;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'S', data.tree_size);
    data.data_size = 60;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    memset(data.compressed_data, 'P', compressed_bytes);
    data.file_name = strdup("special.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("special.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(strcmp(data.original_file, read_data.original_file) == 0);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("special.huf");
    printf("test_file_io_special_chars_in_original_filename passed.\n");
}

void test_file_io_read_nonexistent_file() {
    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    
    int result = read_compressed("nonexistent_file_12345.huf", &read_data, &mmap_ptr);
    
    // Should return error for non-existent file
    assert(result < 0);
    
    printf("test_file_io_read_nonexistent_file passed. Error code: %d\n", result);
}

void test_file_io_very_large_compressed_data() {
    Compressed_file data;
    memcpy(data.magic, magic, sizeof(data.magic));
    data.is_dir = false;
    data.original_size = 1000000;  // 1MB original
    data.original_file = strdup("huge.txt");
    data.tree_size = 100;
    data.huffman_tree = malloc(data.tree_size);
    memset(data.huffman_tree, 'H', data.tree_size);
    
    // Large compressed data (100KB in bits = 819200 bits)
    data.data_size = 819200;
    long compressed_bytes = (long)ceil((double)data.data_size / 8.0);
    data.compressed_data = malloc(compressed_bytes);
    for (long i = 0; i < compressed_bytes; i++) {
        ((char*)data.compressed_data)[i] = (char)(i % 256);
    }
    data.file_name = strdup("huge.huf");

    assert(write_compressed(&data, true) >= 0);

    Compressed_file read_data = {0};
    read_data.original_file = NULL;
    read_data.huffman_tree = NULL;
    read_data.compressed_data = NULL;
    read_data.file_name = NULL;
    const char *mmap_ptr = NULL;
    int mmap_size = read_compressed("huge.huf", &read_data, &mmap_ptr);
    assert(mmap_size > 0);

    assert(data.original_size == read_data.original_size);
    assert(data.data_size == read_data.data_size);
    assert(memcmp(data.compressed_data, read_data.compressed_data, compressed_bytes) == 0);

    munmap((void*)mmap_ptr, mmap_size);
    free(data.original_file);
    free(data.huffman_tree);
    free(data.compressed_data);
    free(data.file_name);
    free(read_data.original_file);
    free(read_data.huffman_tree);
    free(read_data.file_name);

    remove("huge.huf");
    printf("test_file_io_very_large_compressed_data passed.\n");
}

int main() {
    test_file_io();
    
    // Edge case tests
    test_file_io_with_empty_filename();
    test_file_io_with_long_filename();
    test_file_io_with_zero_size_data();
    test_file_io_with_large_tree();
    test_file_io_with_binary_data();
    test_file_io_directory_flag();
    test_file_io_special_chars_in_original_filename();
    test_file_io_read_nonexistent_file();
    test_file_io_very_large_compressed_data();
    
    printf("\nAll edge case tests passed!\n");
    
    return 0;
}

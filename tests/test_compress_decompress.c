#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include "../lib/compress.h"
#include "../lib/decompress.h"
#include "../lib/file.h"
#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"
#include "../lib/directory.h"

static int invoke_run_compression(Arguments args) {
    char *data = NULL;
    long data_len = 0;
    long directory_size = 0;

    if (args.directory) {
        int directory_size_int = 0;
        int prep_res = prepare_directory(args.input_file, &directory_size_int);
        if (prep_res < 0) {
            return prep_res;
        }
        directory_size = directory_size_int;
        int read_res = read_raw(SERIALIZED_TMP_FILE, &data);
        if (read_res < 0) {
            return read_res;
        }
        data_len = read_res;
    } else {
        int read_res = read_raw(args.input_file, &data);
        if (read_res < 0) {
            return read_res;
        }
        data_len = read_res;
        directory_size = data_len;
    }

    int result = run_compression(args, data, data_len, directory_size);
    free(data);
    return result;
}

static int invoke_run_decompression(Arguments args) {
    char *raw_data = NULL;
    long raw_size = 0;
    bool is_dir = false;
    char *original_name = NULL;

    int res = run_decompression(args, &raw_data, &raw_size, &is_dir, &original_name);
    if (res != 0) {
        free(raw_data);
        free(original_name);
        return res;
    }

    if (is_dir) {
        FILE *f = fopen(SERIALIZED_TMP_FILE, "wb");
        if (f == NULL || fwrite(raw_data, 1, raw_size, f) != (size_t)raw_size) {
            printf("Nem sikerult kiirni a szerializalt adatokat.\n");
            if (f != NULL) fclose(f);
            free(raw_data);
            free(original_name);
            return FILE_WRITE_ERROR;
        }
        fclose(f);
        res = restore_directory(args.output_file, args.force, args.no_preserve_perms);
    } else {
        char *target = args.output_file != NULL ? args.output_file : original_name;
        int write_res = write_raw(target, raw_data, raw_size, args.force);
        if (write_res < 0) {
            printf("Hiba tortent a kimeneti fajl (%s) irasa kozben.\n", target);
            res = EIO;
        }
    }

    free(raw_data);
    free(original_name);
    return res;
}

int main() {
    char *data = "hello world";
    long data_len = strlen(data);

    long *frequencies = calloc(256, sizeof(long));
    count_frequencies(data, data_len, frequencies);

    int leaf_count = 0;
    for (int i = 0; i < 256; i++) {
        if (frequencies[i] != 0) {
            leaf_count++;
        }
    }

    Node *nodes = malloc((2 * leaf_count - 1) * sizeof(Node));
    int j = 0;
    for (int i = 0; i < 256; i++) {
        if (frequencies[i] != 0) {
            nodes[j] = construct_leaf(frequencies[i], (char) i);
            j++;
        }
    }
    free(frequencies);

    sort_nodes(nodes, leaf_count);
    Node *root_node = construct_tree(nodes, leaf_count);

    long tree_size;
    if (root_node != NULL) {
        tree_size = (root_node - nodes) + 1;
    }
    else {
        free(nodes);
        return TREE_ERROR;
    }

    char **cache = calloc(256, sizeof(char*));

    Compressed_file *compressed_file = malloc(sizeof(Compressed_file));
    int compress_result = compress(data, data_len, nodes, root_node, cache, compressed_file);
    if (compress_result != 0) {
        printf("Compression failed with error code %d!\n", compress_result);
        free(nodes);
        for(int i=0; i<256; ++i) {
            if (cache[i] != NULL) {
                free(cache[i]);
            }
        }
        free(cache);
        free(compressed_file);
        return 3;
    }

    compressed_file->huffman_tree = nodes;
    compressed_file->tree_size = tree_size * sizeof(Node);

    char *raw_data = malloc(data_len * sizeof(char));
    int decompress_result = decompress(compressed_file, raw_data);
    if (decompress_result != 0) {
        printf("Decompression failed with error code %d!\n", decompress_result);
        free(nodes);
        for(int i=0; i<256; ++i) {
            if (cache[i] != NULL) {
                free(cache[i]);
            }
        }
        free(cache);
        free(compressed_file->compressed_data);
        free(compressed_file);
        free(raw_data);
        return 4;
    }

    if (memcmp(data, raw_data, data_len) != 0) {
        printf("Decompression failed!\n");
        printf("Original:  %s\n", data);
        printf("Decompressed: %s\n", raw_data);
    } else {
        printf("Decompression successful!\n");
    }

    free(nodes);
    for(int i=0; i<256; ++i) {
        if (cache[i] != NULL) {
            free(cache[i]);
        }
    }
    free(cache);
    free(compressed_file->compressed_data);
    free(compressed_file);
    free(raw_data);

    // ==========================================
    // Test run_decompression function
    // ==========================================
    printf("Testing run_decompression function...\n");
    
    // Test 1: Basic run_decompression with file
    printf("  Test 1: Basic run_decompression with file...\n");
    {
        // Create a test file
        char *test_content = "This is a test file for run_decompression testing.";
        char *test_input = "test_decomp_input.txt";
        char *test_compressed = "test_decomp_input.huff";
        char *test_output = "test_decomp_output.txt";
        
        // Write test input file
        int write_res = write_raw(test_input, test_content, strlen(test_content), true);
        if (write_res < 0) {
            fprintf(stderr, "Error: Failed to write test input file\n");
            return 1;
        }
        
        // Use run_compression to create a compressed file
        Arguments compress_args = {0};
        compress_args.compress_mode = true;
        compress_args.extract_mode = false;
        compress_args.force = true;
        compress_args.directory = false;
        compress_args.input_file = test_input;
        compress_args.output_file = NULL;  // Use default output
        
        int comp_result = invoke_run_compression(compress_args);
        if (comp_result != 0) {
            fprintf(stderr, "Error: run_compression failed, code: %d\n", comp_result);
            remove(test_input);
            return 1;
        }
        
        // Now test run_decompression
        Arguments decomp_args = {0};
        decomp_args.compress_mode = false;
        decomp_args.extract_mode = true;
        decomp_args.force = true;
        decomp_args.directory = false;
        decomp_args.input_file = test_compressed;
        decomp_args.output_file = test_output;
        
        int result = invoke_run_decompression(decomp_args);
        if (result != 0) {
            fprintf(stderr, "Error: run_decompression failed, code: %d\n", result);
            remove(test_input);
            remove(test_compressed);
            return 1;
        }
        
        // Verify the decompressed content
        char *decompressed_content = NULL;
        int read_size = read_raw(test_output, &decompressed_content);
        if (read_size < 0) {
            fprintf(stderr, "Error: Failed to read decompressed output file\n");
            remove(test_input);
            remove(test_compressed);
            remove(test_output);
            return 1;
        }
        
        if ((long)read_size != (long)strlen(test_content) || 
            memcmp(test_content, decompressed_content, strlen(test_content)) != 0) {
            fprintf(stderr, "Error: Decompressed content does not match original\n");
            free(decompressed_content);
            remove(test_input);
            remove(test_compressed);
            remove(test_output);
            return 1;
        }
        
        printf("    Basic run_decompression test passed.\n");
        free(decompressed_content);
        
        // Cleanup
        remove(test_input);
        remove(test_compressed);
        remove(test_output);
    }
    
    // Test 2: run_decompression with default output (use original filename)
    printf("  Test 2: run_decompression with default output...\n");
    {
        char *test_content = "Test content for default output test.";
        char *test_input = "test_default_input.txt";
        char *test_compressed = "test_default_input.huff";
        
        // Write test input file
        int write_res = write_raw(test_input, test_content, strlen(test_content), true);
        if (write_res < 0) {
            fprintf(stderr, "Error: Failed to write test input file\n");
            return 1;
        }
        
        // Compress the file
        Arguments compress_args = {0};
        compress_args.compress_mode = true;
        compress_args.extract_mode = false;
        compress_args.force = true;
        compress_args.directory = false;
        compress_args.input_file = test_input;
        compress_args.output_file = NULL;
        
        int comp_result = invoke_run_compression(compress_args);
        if (comp_result != 0) {
            fprintf(stderr, "Error: run_compression failed, code: %d\n", comp_result);
            remove(test_input);
            return 1;
        }
        
        // Remove the original file so decompression recreates it
        remove(test_input);
        
        // Decompress with NULL output (should restore to original filename)
        Arguments decomp_args = {0};
        decomp_args.compress_mode = false;
        decomp_args.extract_mode = true;
        decomp_args.force = true;
        decomp_args.directory = false;
        decomp_args.input_file = test_compressed;
        decomp_args.output_file = NULL;  // Use stored original filename
        
        int result = invoke_run_decompression(decomp_args);
        if (result != 0) {
            fprintf(stderr, "Error: run_decompression with default output failed, code: %d\n", result);
            remove(test_compressed);
            return 1;
        }
        
        // Check that the original file was recreated
        struct stat st;
        if (stat(test_input, &st) != 0) {
            fprintf(stderr, "Error: Original file was not recreated\n");
            remove(test_compressed);
            return 1;
        }
        
        // Verify content
        char *decompressed_content = NULL;
        int read_size = read_raw(test_input, &decompressed_content);
        if (read_size < 0 || (long)read_size != (long)strlen(test_content) ||
            memcmp(test_content, decompressed_content, strlen(test_content)) != 0) {
            fprintf(stderr, "Error: Decompressed content does not match original\n");
            if (decompressed_content != NULL) free(decompressed_content);
            remove(test_input);
            remove(test_compressed);
            return 1;
        }
        
        printf("    run_decompression with default output test passed.\n");
        free(decompressed_content);
        
        // Cleanup
        remove(test_input);
        remove(test_compressed);
    }
    
    // Test 3: run_decompression error handling (non-existent file)
    printf("  Test 3: run_decompression error handling...\n");
    {
        Arguments decomp_args = {0};
        decomp_args.compress_mode = false;
        decomp_args.extract_mode = true;
        decomp_args.force = true;
        decomp_args.directory = false;
        decomp_args.input_file = "non_existent_file_12345.huff";
        decomp_args.output_file = "output.txt";
        
        int result = invoke_run_decompression(decomp_args);
        if (result == 0) {
            fprintf(stderr, "Error: run_decompression should fail for non-existent file\n");
            return 1;
        }
        
        printf("    run_decompression error handling test passed. Error code: %d\n", result);
    }
    
    printf("All run_decompression tests passed!\n");

    // ==========================================
    // EDGE CASE TESTS
    // ==========================================
    printf("Testing edge cases...\n");
    
    // Edge case 1: Compress/decompress single character
    printf("  Edge case 1: Single character...\n");
    {
        char *single_char = "X";
        long single_len = 1;
        
        long *freqs = calloc(256, sizeof(long));
        count_frequencies(single_char, single_len, freqs);
        
        int leaf_cnt = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) leaf_cnt++;
        }
        
        Node *single_nodes = malloc((2 * leaf_cnt - 1) * sizeof(Node));
        int j = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) {
                single_nodes[j++] = construct_leaf(freqs[i], (char)i);
            }
        }
        free(freqs);
        
        sort_nodes(single_nodes, leaf_cnt);
        Node *single_root = construct_tree(single_nodes, leaf_cnt);
        
        char **single_cache = calloc(256, sizeof(char*));
        Compressed_file *single_compressed = malloc(sizeof(Compressed_file));
        
        int comp_res = compress(single_char, single_len, single_nodes, single_root, single_cache, single_compressed);
        assert(comp_res == 0);
        
        single_compressed->huffman_tree = single_nodes;
        single_compressed->tree_size = ((single_root - single_nodes) + 1) * sizeof(Node);
        
        char *single_raw = malloc(single_len);
        int decomp_res = decompress(single_compressed, single_raw);
        assert(decomp_res == 0);
        assert(memcmp(single_char, single_raw, single_len) == 0);
        
        free(single_nodes);
        for (int i = 0; i < 256; i++) {
            if (single_cache[i] != NULL) free(single_cache[i]);
        }
        free(single_cache);
        free(single_compressed->compressed_data);
        free(single_compressed);
        free(single_raw);
        printf("    Single character test passed.\n");
    }
    
    // Edge case 2: Compress/decompress repeating pattern
    printf("  Edge case 2: Repeating pattern...\n");
    {
        char *pattern = "ABABABABABABABABABABABAB";
        long pattern_len = strlen(pattern);
        
        long *freqs = calloc(256, sizeof(long));
        count_frequencies(pattern, pattern_len, freqs);
        
        int leaf_cnt = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) leaf_cnt++;
        }
        
        Node *pattern_nodes = malloc((2 * leaf_cnt - 1) * sizeof(Node));
        int j = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) {
                pattern_nodes[j++] = construct_leaf(freqs[i], (char)i);
            }
        }
        free(freqs);
        
        sort_nodes(pattern_nodes, leaf_cnt);
        Node *pattern_root = construct_tree(pattern_nodes, leaf_cnt);
        
        char **pattern_cache = calloc(256, sizeof(char*));
        Compressed_file *pattern_compressed = malloc(sizeof(Compressed_file));
        
        int comp_res = compress(pattern, pattern_len, pattern_nodes, pattern_root, pattern_cache, pattern_compressed);
        assert(comp_res == 0);
        
        pattern_compressed->huffman_tree = pattern_nodes;
        pattern_compressed->tree_size = ((pattern_root - pattern_nodes) + 1) * sizeof(Node);
        
        char *pattern_raw = malloc(pattern_len);
        int decomp_res = decompress(pattern_compressed, pattern_raw);
        assert(decomp_res == 0);
        assert(memcmp(pattern, pattern_raw, pattern_len) == 0);
        
        free(pattern_nodes);
        for (int i = 0; i < 256; i++) {
            if (pattern_cache[i] != NULL) free(pattern_cache[i]);
        }
        free(pattern_cache);
        free(pattern_compressed->compressed_data);
        free(pattern_compressed);
        free(pattern_raw);
        printf("    Repeating pattern test passed.\n");
    }
    
    // Edge case 3: Compress/decompress all printable ASCII
    printf("  Edge case 3: All printable ASCII...\n");
    {
        char ascii_str[95];
        for (int i = 0; i < 95; i++) {
            ascii_str[i] = (char)(32 + i);
        }
        long ascii_len = 95;
        
        long *freqs = calloc(256, sizeof(long));
        count_frequencies(ascii_str, ascii_len, freqs);
        
        int leaf_cnt = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) leaf_cnt++;
        }
        
        Node *ascii_nodes = malloc((2 * leaf_cnt - 1) * sizeof(Node));
        int j = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) {
                ascii_nodes[j++] = construct_leaf(freqs[i], (char)i);
            }
        }
        free(freqs);
        
        sort_nodes(ascii_nodes, leaf_cnt);
        Node *ascii_root = construct_tree(ascii_nodes, leaf_cnt);
        
        char **ascii_cache = calloc(256, sizeof(char*));
        Compressed_file *ascii_compressed = malloc(sizeof(Compressed_file));
        
        int comp_res = compress(ascii_str, ascii_len, ascii_nodes, ascii_root, ascii_cache, ascii_compressed);
        assert(comp_res == 0);
        
        ascii_compressed->huffman_tree = ascii_nodes;
        ascii_compressed->tree_size = ((ascii_root - ascii_nodes) + 1) * sizeof(Node);
        
        char *ascii_raw = malloc(ascii_len);
        int decomp_res = decompress(ascii_compressed, ascii_raw);
        assert(decomp_res == 0);
        assert(memcmp(ascii_str, ascii_raw, ascii_len) == 0);
        
        free(ascii_nodes);
        for (int i = 0; i < 256; i++) {
            if (ascii_cache[i] != NULL) free(ascii_cache[i]);
        }
        free(ascii_cache);
        free(ascii_compressed->compressed_data);
        free(ascii_compressed);
        free(ascii_raw);
        printf("    All printable ASCII test passed.\n");
    }
    
    // Edge case 4: Compress/decompress with nulls (binary data)
    printf("  Edge case 4: Binary data with nulls...\n");
    {
        char binary_data[20] = {0, 1, 2, 0, 0, 3, 4, 0, 5, 6, 0, 0, 7, 8, 9, 0, 10, 11, 12, 0};
        long binary_len = 20;
        
        long *freqs = calloc(256, sizeof(long));
        count_frequencies(binary_data, binary_len, freqs);
        
        int leaf_cnt = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) leaf_cnt++;
        }
        
        Node *binary_nodes = malloc((2 * leaf_cnt - 1) * sizeof(Node));
        int j = 0;
        for (int i = 0; i < 256; i++) {
            if (freqs[i] != 0) {
                binary_nodes[j++] = construct_leaf(freqs[i], (char)i);
            }
        }
        free(freqs);
        
        sort_nodes(binary_nodes, leaf_cnt);
        Node *binary_root = construct_tree(binary_nodes, leaf_cnt);
        
        char **binary_cache = calloc(256, sizeof(char*));
        Compressed_file *binary_compressed = malloc(sizeof(Compressed_file));
        
        int comp_res = compress(binary_data, binary_len, binary_nodes, binary_root, binary_cache, binary_compressed);
        assert(comp_res == 0);
        
        binary_compressed->huffman_tree = binary_nodes;
        binary_compressed->tree_size = ((binary_root - binary_nodes) + 1) * sizeof(Node);
        
        char *binary_raw = malloc(binary_len);
        int decomp_res = decompress(binary_compressed, binary_raw);
        assert(decomp_res == 0);
        assert(memcmp(binary_data, binary_raw, binary_len) == 0);
        
        free(binary_nodes);
        for (int i = 0; i < 256; i++) {
            if (binary_cache[i] != NULL) free(binary_cache[i]);
        }
        free(binary_cache);
        free(binary_compressed->compressed_data);
        free(binary_compressed);
        free(binary_raw);
        printf("    Binary data with nulls test passed.\n");
    }
    
    // Edge case 5: run_decompression with corrupted file (invalid magic)
    printf("  Edge case 5: Corrupted file handling...\n");
    {
        const char *corrupted_file = "test_corrupted.huff";
        FILE *cf = fopen(corrupted_file, "wb");
        assert(cf != NULL);
        // Write invalid magic
        char bad_magic[8] = "BADMGIC";
        fwrite(bad_magic, 1, 8, cf);
        fclose(cf);
        
        Arguments decomp_args = {0};
        decomp_args.compress_mode = false;
        decomp_args.extract_mode = true;
        decomp_args.force = true;
        decomp_args.directory = false;
        decomp_args.input_file = (char *)corrupted_file;
        decomp_args.output_file = "output_corrupted.txt";
        
        int result = invoke_run_decompression(decomp_args);
        assert(result != 0);  // Should fail
        
        remove(corrupted_file);
        printf("    Corrupted file handling test passed. Error code: %d\n", result);
    }
    
    // Edge case 6: Large file compress/decompress round-trip
    printf("  Edge case 6: Large file round-trip...\n");
    {
        char *large_input = "test_large_roundtrip.txt";
        char *large_compressed = "test_large_roundtrip.huff";
        char *large_output = "test_large_roundtrip_out.txt";
        
        // Create large file with repetitive but varied content
        FILE *lf = fopen(large_input, "w");
        assert(lf != NULL);
        debugmalloc_max_block_size(10 * 1024 * 1024);  // 10MB
        for (int i = 0; i < 10000; i++) {
            fprintf(lf, "Line %d: Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n", i);
        }
        fclose(lf);
        
        // Compress
        Arguments compress_args = {0};
        compress_args.compress_mode = true;
        compress_args.extract_mode = false;
        compress_args.force = true;
        compress_args.directory = false;
        compress_args.input_file = large_input;
        compress_args.output_file = large_compressed;
        
        int comp_result = invoke_run_compression(compress_args);
        assert(comp_result == 0);
        
        // Decompress
        Arguments decomp_args = {0};
        decomp_args.compress_mode = false;
        decomp_args.extract_mode = true;
        decomp_args.force = true;
        decomp_args.directory = false;
        decomp_args.input_file = large_compressed;
        decomp_args.output_file = large_output;
        
        int decomp_result = invoke_run_decompression(decomp_args);
        assert(decomp_result == 0);
        
        // Verify content matches
        char *original_content = NULL;
        char *decompressed_content = NULL;
        int orig_size = read_raw(large_input, &original_content);
        int decomp_size = read_raw(large_output, &decompressed_content);
        
        assert(orig_size == decomp_size);
        assert(memcmp(original_content, decompressed_content, orig_size) == 0);
        
        free(original_content);
        free(decompressed_content);
        remove(large_input);
        remove(large_compressed);
        remove(large_output);
        printf("    Large file round-trip test passed.\n");
    }
    
    printf("All edge case tests passed!\n");

    return 0;
}

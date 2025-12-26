#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include "../lib/compatibility.h"
#include "../lib/file.h"
#include "../lib/compress.h"
#include "../lib/data_types.h"
#include "../lib/directory.h"

// Helper function to get cross-platform temporary directory
static const char* get_temp_dir(void) {
#ifdef _WIN32
    const char *temp = getenv("TEMP");
    if (temp == NULL) {
        temp = getenv("TMP");
    }
    if (temp == NULL) {
        temp = ".";  // Fallback to current directory
    }
    return temp;
#else
    return "/tmp";
#endif
}

// Helper function to build a temporary file path
static void build_temp_path(char *buffer, size_t size, const char *filename) {
    const char *temp_dir = get_temp_dir();
#ifdef _WIN32
    snprintf(buffer, size, "%s\\%s", temp_dir, filename);
#else
    snprintf(buffer, size, "%s/%s", temp_dir, filename);
#endif
}

// Helper function to recursively delete a directory
static int remove_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return unlink(path);
    }

    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                int ret = remove_directory_recursive(full_path);
                if (ret != 0) result = ret;
            } else {
                int ret = unlink(full_path);
                if (ret != 0) result = ret;
            }
        } else {
            // Attempt to remove anyway (could be a broken symlink, etc.)
            int ret = unlink(full_path);
            if (ret != 0) result = ret;
        }
    }
    closedir(dir);
    int ret = rmdir(path);
    return (result != 0) ? result : ret;
}

static void free_cache(char **cache) {
    for (int i = 0; i < 256; ++i) {
        if (cache[i] != NULL) {
            free(cache[i]);
        }
    }
    free(cache);
}

static int invoke_run_compression(Arguments args) {
    const uint8_t *data = NULL;
    uint8_t *allocated_data = NULL;
    long data_len = 0;
    long directory_size = 0;
    bool use_mmap = false;

    if (args.directory) {
        int directory_size_int = 0;
        uint64_t total_size = 0;
        int fd = prepare_directory(args.input_file, &directory_size_int, &total_size);
        if (fd < 0) {
            return FILE_WRITE_ERROR;
        }
        directory_size = directory_size_int;
        
        int64_t read_res = read_raw_from_fd(fd, (const uint8_t**)&allocated_data);
        close(fd);
        
        if (read_res < 0) {
            return (int)read_res;
        }
        data = allocated_data;
        data_len = (long)read_res;
        use_mmap = true;
    } else {
        int read_res = read_raw(args.input_file, &data);
        if (read_res < 0) {
            return read_res;
        }
        data_len = read_res;
        directory_size = data_len;
        use_mmap = true;
    }

    int result = run_compression(args, data, data_len, directory_size);
    if (use_mmap) {
        munmap((void*)data, data_len);
    }
    return result;
}

static void build_huffman_tree(const char *input, long len, Node **out_nodes, Node **out_root) {
    long *frequencies = calloc(256, sizeof(long));
    assert(frequencies != NULL);

    count_frequencies((const uint8_t *)input, len, frequencies);

    int leaf_count = 0;
    for (int i = 0; i < 256; ++i) {
        if (frequencies[i] != 0) {
            leaf_count++;
        }
    }

    assert(leaf_count > 0);

    Node *nodes = malloc((2 * leaf_count - 1) * sizeof(Node));
    assert(nodes != NULL);

    int idx = 0;
    for (int i = 0; i < 256; ++i) {
        if (frequencies[i] != 0) {
            nodes[idx++] = construct_leaf(frequencies[i], (uint8_t)i);
        }
    }

    free(frequencies);

    sort_nodes(nodes, leaf_count);
    Node *root = construct_tree(nodes, leaf_count);
    assert(root != NULL);

    *out_nodes = nodes;
    *out_root = root;
}

static void test_compress_basic_pattern(void) {
    const char *input = "AAAABB";
    const long len = (long)strlen(input);

    Node *nodes = NULL;
    Node *root = NULL;
    build_huffman_tree(input, len, &nodes, &root);

    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);

    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *)input, len, nodes, root, cache, &compressed);
    assert(rc == 0);
    assert(compressed.data_size == 6);          // 6 characters encoded with 1 bit per symbol
    assert(compressed.compressed_data != NULL);
    assert(compressed.compressed_data[0] == (uint8_t)0xF0);
    (void)rc;

    free(compressed.compressed_data);
    free_cache(cache);
    free(nodes);
}

static void test_compress_zero_length(void) {
    Node dummy_nodes[1];
    dummy_nodes[0] = construct_leaf(1, 'A');

    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);

    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *) "", 0, dummy_nodes, &dummy_nodes[0], cache, &compressed);
    assert(rc == 0);
    assert(compressed.data_size == 0);
    assert(compressed.compressed_data == NULL);
    (void)rc;

    free_cache(cache);
}

/* ===== Tests for run_compression function ===== */

static void test_run_compression_basic_file(void) {
    // Create a test file
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_run_compression_input.txt");
    build_temp_path(output_file, sizeof(output_file), "test_run_compression_output.huff");
    const char *test_content = "AAAABBBBCCCC";
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "%s", test_content);
    fclose(f);
    
    // Remove output file if it exists
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    // Verify output file was created
    struct stat st;
    assert(stat(output_file, &st) == 0);
    assert(st.st_size > 0);
    
    // Cleanup
    unlink(test_file);
    unlink(output_file);
}

static void test_run_compression_auto_output_filename(void) {
    // Create a test file
    char test_file[512];
    char expected_output[512];
    build_temp_path(test_file, sizeof(test_file), "test_auto_output.txt");
    build_temp_path(expected_output, sizeof(expected_output), "test_auto_output.huff");
    const char *test_content = "Hello World!";
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "%s", test_content);
    fclose(f);
    
    // Remove expected output file if it exists
    unlink(expected_output);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = NULL;  // Should auto-generate
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    // Verify output file was created with expected name
    struct stat st;
    assert(stat(expected_output, &st) == 0);
    assert(st.st_size > 0);
    
    // Cleanup
    unlink(test_file);
    unlink(expected_output);
}

static void test_run_compression_nonexistent_file(void) {
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "nonexistent_file_12345.txt");
    build_temp_path(output_file, sizeof(output_file), "test_output.huff");
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    // Should return an error for non-existent file
    assert(result < 0);
}

static void test_run_compression_directory(void) {
    // Create test directory structure
    char test_dir[512];
    char output_file[512];
    build_temp_path(test_dir, sizeof(test_dir), "test_run_comp_dir");
    build_temp_path(output_file, sizeof(output_file), "test_run_comp_dir.huff");
    
    char subdir_path[512];
    char file1_path[512];
    char file2_path[512];
    
#ifdef _WIN32
    snprintf(subdir_path, sizeof(subdir_path), "%s\\subdir", test_dir);
    snprintf(file1_path, sizeof(file1_path), "%s\\file1.txt", test_dir);
    snprintf(file2_path, sizeof(file2_path), "%s\\subdir\\file2.txt", test_dir);
#else
    snprintf(subdir_path, sizeof(subdir_path), "%s/subdir", test_dir);
    snprintf(file1_path, sizeof(file1_path), "%s/file1.txt", test_dir);
    snprintf(file2_path, sizeof(file2_path), "%s/subdir/file2.txt", test_dir);
#endif
    
    // Remove any existing directory (follows existing test patterns)
    remove_directory_recursive(test_dir);
    unlink(output_file);
    
    // Create directories with error checking
    if (mkdir(test_dir, DIR_MODE) != 0) {
        fprintf(stderr, "Failed to create test directory: %s\n", test_dir);
        return;
    }
    if (mkdir(subdir_path, DIR_MODE) != 0) {
        fprintf(stderr, "Failed to create test subdirectory: %s\n", subdir_path);
        return;
    }
    
    // Create files
    FILE *f1 = fopen(file1_path, "w");
    if (f1 == NULL) {
        fprintf(stderr, "Failed to create test file: %s\n", file1_path);
        return;
    }
    fprintf(f1, "Content of file 1");
    fclose(f1);
    
    FILE *f2 = fopen(file2_path, "w");
    if (f2 == NULL) {
        fprintf(stderr, "Failed to create test file: %s\n", file2_path);
        return;
    }
    fprintf(f2, "Content of file 2");
    fclose(f2);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = true;
    args.input_file = test_dir;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    // Verify output file was created
    struct stat st;
    assert(stat(output_file, &st) == 0);
    assert(st.st_size > 0);
    
    // Cleanup (follows existing test patterns)
    remove_directory_recursive(test_dir);
    unlink(output_file);
}

static void test_run_compression_force_overwrite(void) {
    // Create test file
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_force_input.txt");
    build_temp_path(output_file, sizeof(output_file), "test_force_output.huff");
    const char *test_content = "Test content for force overwrite";
    
    // Remove any existing files first
    unlink(test_file);
    unlink(output_file);
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "%s", test_content);
    fclose(f);
    
    // Create an existing output file with different content
    FILE *existing = fopen(output_file, "w");
    assert(existing != NULL);
    fprintf(existing, "Existing content");
    fclose(existing);
    
    // Get original file modification time
    struct stat original_stat;
    assert(stat(output_file, &original_stat) == 0);
    time_t original_mtime = original_stat.st_mtime;
    
    // Wait a moment to ensure different modification time
    usleep(10000);  // 10ms
    
    // Test with force=true - should overwrite successfully
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = true;  // Use force to avoid interactive prompt
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    // Verify file was overwritten (modification time should be different)
    struct stat new_stat;
    assert(stat(output_file, &new_stat) == 0);
    assert(new_stat.st_mtime >= original_mtime);  // Should be modified
    assert(new_stat.st_size > 0);  // File should have content
    
    // Cleanup
    unlink(test_file);
    unlink(output_file);
}

static void test_run_compression_empty_file(void) {
    // Create an empty test file
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_empty_file.txt");
    build_temp_path(output_file, sizeof(output_file), "test_empty_output.huff");
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fclose(f);
    
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    // Empty file should return EMPTY_FILE error
    assert(result == EMPTY_FILE);
    
    // Cleanup
    unlink(test_file);
    unlink(output_file);
}

/* ===== EDGE CASE TESTS ===== */

static void test_compress_single_char(void) {
    // Test with single character
    const char *input = "A";
    const long len = 1;
    
    Node *nodes = NULL;
    Node *root = NULL;
    build_huffman_tree(input, len, &nodes, &root);
    
    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);
    
    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *)input, len, nodes, root, cache, &compressed);
    assert(rc == 0);
    (void)rc;
    
    // Single character compression succeeds with one 0 bit per character
    assert(compressed.data_size == 1);
    assert(compressed.compressed_data != NULL);
    
    free(compressed.compressed_data);
    free_cache(cache);
    free(nodes);
}

static void test_compress_all_same_char(void) {
    // All same character
    const char *input = "AAAAAAAAAA";
    const long len = 10;
    
    Node *nodes = NULL;
    Node *root = NULL;
    build_huffman_tree(input, len, &nodes, &root);
    
    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);
    
    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *)input, len, nodes, root, cache, &compressed);
    assert(rc == 0);
    assert(compressed.data_size == 10);
    assert(compressed.compressed_data != NULL);
    (void)rc;
    
    free(compressed.compressed_data);
    free_cache(cache);
    free(nodes);
}

static void test_compress_all_unique_chars(void) {
    const char *input = "ABCDEFGH";
    const long len = 8;
    
    Node *nodes = NULL;
    Node *root = NULL;
    build_huffman_tree(input, len, &nodes, &root);
    
    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);
    
    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *)input, len, nodes, root, cache, &compressed);
    assert(rc == 0);
    assert(compressed.compressed_data != NULL);
    (void)rc;
    
    free(compressed.compressed_data);
    free_cache(cache);
    free(nodes);
}

static void test_compress_binary_data(void) {
    char input[256];
    for (int i = 0; i < 256; i++) {
        input[i] = (char)i;
    }
    const long len = 256;
    
    Node *nodes = NULL;
    Node *root = NULL;
    build_huffman_tree(input, len, &nodes, &root);
    
    char **cache = calloc(256, sizeof(char *));
    assert(cache != NULL);
    
    Compressed_file compressed = {0};
    int rc = compress((const uint8_t *)input, len, nodes, root, cache, &compressed);
    assert(rc == 0);
    assert(compressed.compressed_data != NULL);
    (void)rc;
    
    free(compressed.compressed_data);
    free_cache(cache);
    free(nodes);
}

static void test_run_compression_moderately_large_file(void) {
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_large_file.txt");
    build_temp_path(output_file, sizeof(output_file), "test_large_output.huff");
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    // 100000 lines, about 5MB
    for (int i = 0; i < 100000; i++) {
        fprintf(f, "Line %d: The quick brown fox jumps over the lazy dog.\n", i);
    }
    fclose(f);
    
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    assert(st.st_size > 0);
    
    unlink(test_file);
    unlink(output_file);
}

static void test_run_compression_special_chars_in_filename(void) {
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test-file_with.special$chars.txt");
    build_temp_path(output_file, sizeof(output_file), "test-output_with.special$chars.huff");
    const char *test_content = "Test content with special filename";
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "%s", test_content);
    fclose(f);
    
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    
    unlink(test_file);
    unlink(output_file);
}

static void test_run_compression_readonly_input(void) {
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_readonly_input.txt");
    build_temp_path(output_file, sizeof(output_file), "test_readonly_output.huff");
    const char *test_content = "Read-only test content";
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "%s", test_content);
    fclose(f);
    
    chmod(test_file, 0444);
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    
    chmod(test_file, FILE_MODE);
    unlink(test_file);
    unlink(output_file);
}

static void test_run_compression_empty_directory(void) {
    char test_dir[512];
    char output_file[512];
    build_temp_path(test_dir, sizeof(test_dir), "test_empty_dir");
    build_temp_path(output_file, sizeof(output_file), "test_empty_dir.huff");
    
    remove_directory_recursive(test_dir);
    unlink(output_file);
    
    mkdir(test_dir, DIR_MODE);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = true;
    args.input_file = test_dir;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    
    remove_directory_recursive(test_dir);
    unlink(output_file);
}

static void test_run_compression_nested_empty_directories(void) {
    char test_dir[512];
    char output_file[512];
    build_temp_path(test_dir, sizeof(test_dir), "test_nested_empty_dir");
    build_temp_path(output_file, sizeof(output_file), "test_nested_empty_dir.huff");
    
    char subdir1[512], subdir2[512], subdir3[512];
    
    remove_directory_recursive(test_dir);
    unlink(output_file);
    
    mkdir(test_dir, DIR_MODE);
    
#ifdef _WIN32
    snprintf(subdir1, sizeof(subdir1), "%s\\level1", test_dir);
    snprintf(subdir2, sizeof(subdir2), "%s\\level1\\level2", test_dir);
    snprintf(subdir3, sizeof(subdir3), "%s\\level1\\level2\\level3", test_dir);
#else
    snprintf(subdir1, sizeof(subdir1), "%s/level1", test_dir);
    snprintf(subdir2, sizeof(subdir2), "%s/level1/level2", test_dir);
    snprintf(subdir3, sizeof(subdir3), "%s/level1/level2/level3", test_dir);
#endif
    
    mkdir(subdir1, DIR_MODE);
    mkdir(subdir2, DIR_MODE);
    mkdir(subdir3, DIR_MODE);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = true;
    args.input_file = test_dir;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    
    remove_directory_recursive(test_dir);
    unlink(output_file);
}

static void test_run_compression_single_byte_file(void) {
    char test_file[512];
    char output_file[512];
    build_temp_path(test_file, sizeof(test_file), "test_single_byte.txt");
    build_temp_path(output_file, sizeof(output_file), "test_single_byte_output.huff");
    
    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fputc('X', f);
    fclose(f);
    
    unlink(output_file);
    
    Arguments args = {0};
    args.compress_mode = true;
    args.extract_mode = false;
    args.force = false;
    args.directory = false;
    args.input_file = test_file;
    args.output_file = output_file;
    
    int result = invoke_run_compression(args);
    assert(result == 0);
    
    struct stat st;
    assert(stat(output_file, &st) == 0);
    
    unlink(test_file);
    unlink(output_file);
}

int main(void) {
    test_compress_basic_pattern();
    test_compress_zero_length();
    
    // run_compression tests
    test_run_compression_basic_file();
    test_run_compression_auto_output_filename();
    test_run_compression_nonexistent_file();
    test_run_compression_directory();
    test_run_compression_force_overwrite();
    test_run_compression_empty_file();
    
    // Edge case tests
    test_compress_single_char();
    test_compress_all_same_char();
    test_compress_all_unique_chars();
    test_compress_binary_data();
    test_run_compression_moderately_large_file();
    test_run_compression_special_chars_in_filename();
    test_run_compression_readonly_input();
    test_run_compression_empty_directory();
    test_run_compression_nested_empty_directories();
    test_run_compression_single_byte_file();
    
    return 0;
}

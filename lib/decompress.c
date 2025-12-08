#include "debugmalloc.h"
#include "data_types.h"
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "file.h"
#include "decompress.h"

/*
 * Traverses the Huffman tree to recreate the original data bit by bit.
 * Reads from the compressed buffer and writes the decompressed bytes into the caller-provided array.
 * Returns 0 on success or a negative value on failure.
 */
int decompress(Compressed_file *compressed, char *raw) {
    long root_index = (compressed->tree_size / sizeof(Node)) - 1;

    if (root_index < 0) {
        return TREE_ERROR;
    }

    long current_node = root_index;
    long current_raw = 0;

    // Check whether the root is a leaf (the single unique character case).
    bool root_is_leaf = (compressed->huffman_tree[root_index].type == LEAF);

    unsigned char buffer = 0;
    for (long i = 0; i < compressed->data_size; i++) {
        if (current_raw >= compressed->original_size) {
            break;
        }

        if (i % 8 == 0) {
            buffer = compressed->compressed_data[i / 8];
        }

        // If the root is a leaf, every bit yields the same character.
        if (root_is_leaf) {
            raw[current_raw++] = compressed->huffman_tree[root_index].data;
        } else {
            if (buffer & (1 << (7 - i % 8))) {
                current_node = compressed->huffman_tree[current_node].right;
            } else {
                current_node = compressed->huffman_tree[current_node].left;
            }

            if (compressed->huffman_tree[current_node].type == LEAF) {
                raw[current_raw++] = compressed->huffman_tree[current_node].data;
                current_node = root_index;
            }
        }
    }

    return 0;
}

/*
 * Reads the compressed file, decodes the Huffman data, and returns the raw content.
 * For files: creates a memory-mapped output file and decompresses directly to it, then unmaps.
 * For directories: allocates a buffer for serialized data (caller must free).
 * Output pointer arguments must be valid addresses; the function allocates and assigns the data.
 */
int run_decompression(Arguments args, char **raw_data, long *raw_size, bool *is_directory, char **original_name) {
    *raw_data = NULL;
    *raw_size = 0;
    *is_directory = false;
    *original_name = NULL;

    Compressed_file *compressed_file = NULL;
    const char *mmap_ptr = NULL;
    long mmap_size = 0;
    char *output_mmap = NULL;
    long output_mmap_size = 0;
    int res = 0;

    while (true) {
        compressed_file = calloc(1, sizeof(Compressed_file));
        if (compressed_file == NULL) {
            fprintf(stderr, "Failed to allocate memory.\n");
            res = ENOMEM;
            break;
        }

        int read_res = read_compressed(args.input_file, compressed_file, &mmap_ptr);
        if (read_res < 0) {
            if (read_res == FILE_MAGIC_ERROR) {
                fprintf(stderr, "The compressed file (%s) is corrupted and could not be read.\n", args.input_file);
                res = EBADF;
            } else if (read_res == MALLOC_ERROR) {
                fprintf(stderr, "Failed to allocate memory.\n");
                res = ENOMEM;
            } else {
                fprintf(stderr, "Failed to read the compressed file (%s).\n", args.input_file);
                res = EIO;
            }
            break;
        }
        mmap_size = read_res;

        if (compressed_file->original_size <= 0) {
            fprintf(stderr, "The compressed file (%s) is corrupted and could not be read.\n", args.input_file);
            res = EINVAL;
            break;
        }

        *is_directory = compressed_file->is_dir;
        *original_name = strdup(compressed_file->original_file);
        if (*original_name == NULL) {
            fprintf(stderr, "Failed to allocate memory.\n");
            res = ENOMEM;
            break;
        }

        if (compressed_file->is_dir) {
            *raw_data = malloc(compressed_file->original_size * sizeof(char));
            if (*raw_data == NULL) {
                fprintf(stderr, "Failed to allocate memory.\n");
                res = ENOMEM;
                break;
            }
        } else {
            char *target = args.output_file != NULL ? args.output_file : compressed_file->original_file;
            int write_res = write_raw(target, raw_data, compressed_file->original_size, args.force);
            if (write_res < 0) {
                if (write_res == FILE_WRITE_ERROR) {
                    fprintf(stderr, "Failed to write the output file (%s).\n", target);
                    res = EIO;
                } else if (write_res == SCANF_FAILED) {
                    fprintf(stderr, "Failed to read the response.\n");
                    res = EIO;
                } else if (write_res == NO_OVERWRITE) {
                    fprintf(stderr, "The file was not overwritten.\n");
                    res = ECANCELED;
                } else {
                    fprintf(stderr, "An error occurred while writing the output file (%s).\n", target);
                    res = EIO;
                }
                break;
            }
            output_mmap = *raw_data;
            output_mmap_size = write_res;
        }

        int decompress_result = decompress(compressed_file, *raw_data);
        if (decompress_result != 0) {
            fprintf(stderr, "Failed to decompress.\n");
            res = EIO;
            break;
        }

        *raw_size = compressed_file->original_size;
        break;
    }

    if (mmap_ptr != NULL) {
        munmap((void*)mmap_ptr, mmap_size);
    }

    if (output_mmap != NULL) {
        if (msync(output_mmap, output_mmap_size, MS_SYNC) == -1) {
            fprintf(stderr, "Warning: Failed to sync output file.\n");
        }
        munmap(output_mmap, output_mmap_size);
        if (res == 0) {
            *raw_data = NULL;
        }
    }

    if (compressed_file != NULL) {
        free(compressed_file->file_name);
        free(compressed_file->original_file);
        free(compressed_file);
    }

    if (res != 0) {
        if (*raw_data != NULL && !*is_directory) {
            *raw_data = NULL;
        } else if (*raw_data != NULL && *is_directory) {
            free(*raw_data);
            *raw_data = NULL;
        }
        if (*original_name != NULL) {
            free(*original_name);
            *original_name = NULL;
        }
    }

    return res;
}

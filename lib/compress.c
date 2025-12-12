#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include "file.h"
#include "compress.h"
#include "data_types.h"
#include "directory.h"
#include "debugmalloc.h"

// Helper for sorting with qsort.
static int compare_nodes(const void *a, const void *b) {
    long freq_a = ((Node*)a)->frequency;
    long freq_b = ((Node*)b)->frequency;
    if (freq_a < freq_b) return -1;
    if (freq_a > freq_b) return 1;
    return 0;
}

// Sorts the nodes array by frequency to simplify building the Huffman tree.
void sort_nodes(Node *nodes, int len) {
    qsort(nodes, len, sizeof(Node), compare_nodes);
}

/*
 * Iterates through the raw data and increments the 256-element frequency array in place.
 * Returns 0 after processing every byte; the caller must supply a zeroed frequencies array.
 */
int count_frequencies(const char *data, long data_len, long *frequencies) {
    for (size_t i = 0; i < (size_t)data_len; i++){
        frequencies[(unsigned char) data[i]] += 1;
    }
    return 0;
}

/*
 * Builds the output file name: replaces the extension with .huff if present, otherwise appends it.
 * Returns an allocated string on success or NULL on failure.
 */
char* generate_output_file(char *input_file){
    char *dir_end = strrchr(input_file, '/');
    char *name_end;
    if (dir_end != NULL) name_end = strrchr(dir_end, '.');
    else name_end = strrchr(input_file, '.');

    char *out;
    if (name_end != NULL) {
        size_t name_len = name_end - input_file;
        out = malloc(name_len + 6);
        if (out == NULL) {
            return NULL;
        }
        strncpy(out, input_file, name_len);
        out[name_len] = '\0';
        strcat(out, ".huff");
    }
    else {
        out = malloc(strlen(input_file) + 6);
        if (out == NULL) {
            return NULL;
        }
        strcpy(out, input_file);

        strcat(out, ".huff");
    }
    return out;
}

// Creates a leaf that stores the byte and its associated frequency.
Node construct_leaf(long frequency, char data) {
    Node leaf = {0};
    leaf.type = LEAF;
    leaf.frequency = frequency;
    leaf.data = data;
    return leaf;
}

/*
 * Builds a branch node that stores the indices of its two children in the array.
 * Per Huffman rules, its frequency is the sum of the children's frequencies.
 */
Node construct_branch(Node *nodes, int left_index, int right_index) {
    Node branch = {0};
    branch.type = BRANCH;
    branch.left = left_index;
    branch.right = right_index;
    branch.frequency = nodes[left_index].frequency + nodes[right_index].frequency;
    return branch;
}

/*
 * Merges the sorted leaves to build a Huffman tree.
 * Returns a pointer to the root or NULL if there are no leaves.
 * Places created branches after the leaves to keep the list ordered.
 */
Node* construct_tree(Node *nodes, long leaf_count) { // nodes is sorted
    if (leaf_count <= 0) return NULL;
    if (leaf_count == 1) {
        return &nodes[0];
    }
    long current_leaf = 0;
    long current_branch = leaf_count;
    long last_branch = leaf_count;

    for (int i = 0; i < leaf_count - 1; i++) {
        int left_index, right_index;

        if (current_leaf < leaf_count && (current_branch == last_branch || nodes[current_leaf].frequency <= nodes[current_branch].frequency)) {
            left_index = current_leaf++;
        } else {
            left_index = current_branch++;
        }

        if (current_leaf < leaf_count && (current_branch == last_branch || nodes[current_leaf].frequency <= nodes[current_branch].frequency)) {
            right_index = current_leaf++;
        } else {
            right_index = current_branch++;
        }

        nodes[last_branch] = construct_branch(nodes, left_index, right_index);
        last_branch++;
    }
    return &nodes[last_branch - 1];
}

/*
 * Checks whether the sought byte's path in the Huffman tree is already cached.
 * Returns the path string if present, otherwise NULL.
 */
char* check_cache(char leaf, char **cache) {
    if (cache[(unsigned char) leaf] != NULL) return cache[(unsigned char) leaf];
    else return NULL;
}

/*
 * Recursively traverses the Huffman tree, finds the byte's position, and builds the path.
 */
char* find_leaf(char leaf, Node *nodes, Node *root_node) {
    char *path = NULL; 
    if (root_node->type == LEAF) {
        if (root_node->data == leaf) {
            path = malloc(1);
            if (path != NULL) {
                path[0] = '\0';
            }
        }
        return path;
    }
    else {
        char *res = find_leaf(leaf, nodes, &nodes[root_node->left]);
        if (res != NULL) {
            path = malloc((strlen(res) + 2) * sizeof(char));
            if (path != NULL) {
                strcpy(path, "0");
                strcat(path, res);
            }
            free(res);
            return path;
        }
        res = find_leaf(leaf, nodes, &nodes[root_node->right]);
        if (res != NULL) {
            path = malloc((strlen(res) + 2) * sizeof(char));
            if (path != NULL) {
                strcpy(path, "1");
                strcat(path, res);
            }
            free(res);
            return path;
        }
        return NULL;
    }
}


/*
 * Walks the Huffman tree and encodes the data into a compressed bitstream.
 * Loads the data needed for decompression into a Compressed_file structure.
 * Returns 0 on success or a negative value for allocation or traversal errors.
 */
int compress(const char *original_data, long data_len, Node *nodes, Node *root_node, char** cache, Compressed_file *compressed_file) {
    if (data_len == 0) {
        compressed_file->data_size = 0;
        compressed_file->compressed_data = NULL;
        return 0;
    }

    compressed_file->compressed_data = malloc(data_len * sizeof(char));
    if (compressed_file->compressed_data == NULL) {
        compressed_file->data_size = 0;
        return MALLOC_ERROR;
    }

    size_t total_bits = 0;
    unsigned char buffer = 0;
    int bit_count = 0;

    for (size_t i = 0; i < (size_t)data_len; i++) {
        char *path = check_cache(original_data[i], cache);
        if (path == NULL) {
            path = find_leaf(original_data[i], nodes, root_node);
            if (path != NULL) {
                cache[(unsigned char)original_data[i]] = path;
            } else {
                free(compressed_file->compressed_data);
                compressed_file->compressed_data = NULL;
                compressed_file->data_size = 0;
                return TREE_ERROR;
            }
        }

        for (int j = 0; path[j] != '\0'; j++) {
            if (path[j] == '1') {
                buffer |= (1 << (7 - bit_count));
            }
            bit_count++;
            if (bit_count == 8) {
                compressed_file->compressed_data[total_bits / 8] = buffer;
                total_bits += 8;
                buffer = 0;
                bit_count = 0;
            }
        }
    }

    if (bit_count > 0) {
        compressed_file->compressed_data[total_bits / 8] = buffer;
        total_bits += bit_count;
    }

    /* Special case: if only a single unique character exists, the tree depth is 0,
     * so no bits would be generated. Write a 0 bit for each character to record the length. */
    if (total_bits == 0 && data_len > 0) {
        buffer = 0;
        bit_count = 0;
        for (size_t i = 0; i < (size_t)data_len; i++) {
            // Write a 0 bit (do not set a bit in the buffer)
            bit_count++;
            if (bit_count == 8) {
                compressed_file->compressed_data[total_bits / 8] = buffer;
                total_bits += 8;
                buffer = 0;
                bit_count = 0;
            }
        }
        if (bit_count > 0) {
            compressed_file->compressed_data[total_bits / 8] = buffer;
            total_bits += bit_count;
        }
    }

    compressed_file->data_size = total_bits;

    size_t final_size = (size_t)ceil((double)total_bits / 8.0);
    char *temp = realloc(compressed_file->compressed_data, final_size);
    if (temp != NULL) {
        compressed_file->compressed_data = temp;
    }

    return 0;
}

/*
 * Uses the prepared raw data to build a Huffman tree and write the compressed output.
 * The caller must supply the raw data beforehand (file read, directory serialization).
 * Reads directory mode from args.directory. Returns 0 on success or a negative error code.
 */
int run_compression(Arguments args, const char *data, long data_len, long directory_size) {
    // If the user did not provide an output file, generate one.
    bool output_generated = false;
    if (args.output_file == NULL) {
        output_generated = true;
        args.output_file = generate_output_file(args.input_file);
        if (args.output_file == NULL) {
            fputs("Failed to allocate memory.\n", stderr);
            return ENOMEM;
        }
    }

    int write_res = 0;
    long *frequencies = NULL;
    Compressed_file *compressed_file = NULL;
    Node *nodes = NULL;
    long tree_size = 0;
    char **cache = NULL;
    int res = 0;
    
    // The loop always breaks at the end; on errors we jump to the end.
    while (true) {
        // Count the frequency of each byte in the input data.
        frequencies = calloc(256, sizeof(long));
        if (frequencies == NULL) {
            fputs("Failed to allocate memory.\n", stderr);
            res = MALLOC_ERROR;
            break;
        }
        count_frequencies(data, data_len, frequencies);

        int leaf_count = 0;
        for (int i = 0; i < 256; i++) {
            if (frequencies[i] != 0) {
                leaf_count++;
            }
        }

        if (leaf_count == 0) {
            fputs("The file (", stderr);
            fputs(args.input_file, stderr);
            fputs(") is empty.\n", stderr);
            res = SUCCESS;
            break;
        }

        nodes = malloc((2 * leaf_count - 1) * sizeof(Node));
        if (nodes == NULL) {
            fputs("Failed to allocate memory.\n", stderr);
            res = MALLOC_ERROR;
            break;
        }

        int j = 0;
        for (int i = 0; i < 256; i++) {
            if (frequencies[i] != 0) {
                nodes[j] = construct_leaf(frequencies[i], (char)i);
                j++;
            }
        }
        free(frequencies);
        frequencies = NULL;

        // Build the Huffman tree from the sorted leaves.
        sort_nodes(nodes, leaf_count);
        Node *root_node = construct_tree(nodes, leaf_count);

        if (root_node != NULL) {
            tree_size = (root_node - nodes) + 1;
        } else {
            fputs("Failed to build the Huffman tree.\n", stderr);
            res = TREE_ERROR;
            break;
        }
        cache = calloc(256, sizeof(char *));
        if (cache == NULL) {
            fputs("Failed to allocate memory.\n", stderr);
            res = MALLOC_ERROR;
            break;
        }

        compressed_file = malloc(sizeof(Compressed_file));
        if (compressed_file == NULL) {
            fputs("Failed to allocate memory.\n", stderr);
            res = MALLOC_ERROR;
            break;
        }
        // Compress the read data into the compressed_file structure.
        int compress_res = compress(data, data_len, nodes, root_node, cache, compressed_file);
        if (compress_res != 0) {
            fputs("Failed to compress.\n", stderr);
            res = compress_res;
            break;
        }

        compressed_file->is_dir = args.directory;

        compressed_file->huffman_tree = nodes;
        compressed_file->tree_size = tree_size * sizeof(Node);
        compressed_file->original_file = args.input_file;
        compressed_file->original_size = data_len;
        compressed_file->file_name = args.output_file;
        write_res = write_compressed(compressed_file, args.force);
        if (write_res < 0) {
            if (write_res == NO_OVERWRITE) {
                fputs("The file was not overwritten; compression was not performed.\n", stderr);
                write_res = ECANCELED;
            } else if (write_res == MALLOC_ERROR) {
                fputs("Failed to allocate memory.\n", stderr);
                write_res = ENOMEM;
            } else if (write_res == SCANF_FAILED) {
                fputs("Failed to read the response.\n", stderr);
                write_res = EIO;
            } else {
                fputs("Failed to write the output file (", stderr);
                fputs(compressed_file->file_name, stderr);
                fputs(").\n", stderr);
                write_res = EIO;
            }
        }
        else {
            size_t original_size = (size_t)data_len;
            size_t compressed_size = write_res;
            const char *original_unit = get_unit(&original_size);
            const char *compressed_unit = get_unit(&compressed_size);
            long denominator = args.directory ? directory_size : data_len;
            double ratio = (denominator > 0) ? ((double)write_res / denominator * 100) : 0.0;
            printf("Compression complete.\n"
                    "Original size:    %zu%s\n"
                    "Compressed size:  %zu%s\n"
                    "Compression ratio: %.2f%%\n", original_size, original_unit,
                                                 compressed_size, compressed_unit,
                                                 ratio);
        }
        break;
    }
    free(frequencies);
    if (output_generated) free(args.output_file);
    free(nodes);
    if (compressed_file != NULL) {
        free(compressed_file->compressed_data);
        free(compressed_file);
    }

    if (cache != NULL) {
        for (int i = 0; i < 256; ++i) {
            if (cache[i] != NULL) {
                free(cache[i]);
            }
        }
        free(cache);
    }
    if (write_res < 0) res = write_res;
    return res;
}

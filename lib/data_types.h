#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Magic value used as the identifier stored in the compressed file.
 */
static const char magic[4] = {'H', 'U', 'F', 'F'};

#define SERIALIZED_TMP_FILE ".serialized.tmp"


// Indicates whether a node is a leaf (stores data) or a branch.
typedef enum {
    LEAF,
    BRANCH
} Node_type;


// A node in the Huffman tree; stores either the character or the indices of its two children in a union.
typedef struct Node {
    Node_type type;
    long frequency;
    union {
        char data;
        struct {
            int left;
            int right;
        };
    };
} Node;

/*
 * Contains all key data of the compressed file: the identifier, file names, tree, compressed data, and sizes.
 * The compress/decompress and read/write_compressed functions interpret this structure.
 * Stores the compressed data size in bits so it can track partial bytes (for example, 21 bits).
 */
typedef struct {
    char magic[4];
    bool is_dir;
    char *file_name;
    size_t original_size;
    char *original_file;
    Node *huffman_tree;
    size_t tree_size; 
    char *compressed_data;
    size_t data_size; // In bits.
} Compressed_file;

// Holds the error codes for the helper functions.
typedef enum {
    SUCCESS = 0,
    HELP_REQUESTED = 1,
    MALLOC_ERROR = -1,
    FILE_READ_ERROR = -2,
    FILE_MAGIC_ERROR = -3,
    TREE_ERROR = -4,
    FILE_WRITE_ERROR = -5,
    DECOMPRESSION_ERROR = -6,
    COMPRESSION_ERROR = -7,
    NO_OVERWRITE = -8,
    SCANF_FAILED = -9,
    DIRECTORY_OPEN_ERROR = -10,
    EMPTY_DIRECTORY = -11,
    MKDIR_ERROR = -12,
    DIRECTORY_ERROR = -13,
    EMPTY_FILE = -14
} Error_code;

typedef struct {
    bool is_dir;
    union {
        struct {
            char *dir_path;
            int perms;
        };
        struct {
            size_t file_size;
            char *file_path;
            char *file_data;
        };
    };
} Directory_item;

typedef struct {
    bool compress_mode;
    bool extract_mode;
    bool force;
    bool directory;
    bool no_preserve_perms;
    char *input_file;
    char *output_file;
} Arguments;

#endif

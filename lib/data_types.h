#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdbool.h>

/*
 * A magic az a tomoritett fajlban szereplo azonosito.
 */
static const char magic[4] = {'H', 'U', 'F', 'F'};

#define SERIALIZED_TMP_FILE ".serialized.tmp"


// Jelzi, hogy egy node level (adatot tartalmaz) vagy csomopont.
typedef enum {
    LEAF,
    BRANCH
} Node_type;


// A Huffman fa egy pontja: unionban tarolja a karaktert vagy a ket gyermek indexet.
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
 * A tomoritett fajl minden fontos adatat tartalmazza: az azonosito szam, fajlnevek, fa, tomoritett adat es meretek.
 * A compress es decompress, valamint a read es write_compressed funkciok ezt a strukturat ertelmezik.
 * A tomoritett adat meretet bitekben tarolja, igy tudja kezelni a nem teljes bajtnyi tomoritett adatot. (pl. 21 bit)
 */
typedef struct {
    char magic[4];
    bool is_dir;
    char *file_name;
    long original_size;
    char *original_file;
    Node *huffman_tree;
    long tree_size; 
    char *compressed_data;
    long data_size; // In bits.
} Compressed_file;

// A segedfuggvenyek hibakodjait tarolja.
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
            long file_size;
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

#include "debugmalloc.h"
#include "data_types.h"
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "file.h"
#include "decompress.h"
#include "directory.h"

/*
 * A Huffman fat bejarva ujra eloallitja az eredeti adatokat bitrol bitre.
 * A tomoritett bufferbol olvas, es a kitomoritett bajtokat a hivo altal adott tombbe irja.
 * Sikeres kitomorites eseten 0-t, hibak eseten negativ szamokat ad vissza.
 */
int decompress(Compressed_file *compressed, char *raw) {
    long root_index = (compressed->tree_size / sizeof(Node)) - 1;

    if (root_index < 0) {
        return TREE_ERROR;
    }

    long current_node = root_index;
    long current_raw = 0;

    // Ellenorzi, hogy a gyoker level-e (egyetlen egyedi karakter esete).
    bool root_is_leaf = (compressed->huffman_tree[root_index].type == LEAF);

    unsigned char buffer = 0;
    for (long i = 0; i < compressed->data_size; i++) {
        if (current_raw >= compressed->original_size) {
            break;
        }

        if (i % 8 == 0) {
            buffer = compressed->compressed_data[i / 8];
        }

        // Ha a gyoker level, minden bit ugyanazt a karaktert adja vissza.
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
 * Beolvassa a tomoritett fajlt, dekodolja a Huffman adatokat es visszaadja a nyers tartalmat.
 * A kimenet feldolgozasarol (fajl iras, mappa visszaallitasa) a hivo gondoskodik. A ki-
 * menetkent adott pointereknek ervenyes, nem NULL ertekeknek kell lenniuk, mert a hivo
 * (a fo orchestracio) szallitja oket.
 */
int run_decompression(Arguments args, char **raw_data, long *raw_size, bool *is_directory, char **original_name) {
    *raw_data = NULL;
    *raw_size = 0;
    *is_directory = false;
    *original_name = NULL;

    Compressed_file *compressed_file = NULL;
    int res = 0;

    while (true) {
        compressed_file = calloc(1, sizeof(Compressed_file));
        if (compressed_file == NULL) {
            printf("Nem sikerult lefoglalni a memoriat.\n");
            res = ENOMEM;
            break;
        }

        int read_res = read_compressed(args.input_file, compressed_file);
        if (read_res != 0) {
            if (read_res == FILE_MAGIC_ERROR) {
                printf("A tomoritett fajl (%s) serult, nem sikerult beolvasni.\n", args.input_file);
                res = EBADF;
            } else if (read_res == MALLOC_ERROR) {
                printf("Nem sikerult lefoglalni a memoriat.\n");
                res = ENOMEM;
            } else if (read_res == FILE_READ_ERROR) {
                printf("Nem sikerult beolvasni a tomoritett fajlt (%s).\n", args.input_file);
                res = EIO;
            } else {
                printf("Nem sikerult beolvasni a tomoritett fajlt (%s).\n", args.input_file);
                res = EIO;
            }
            break;
        }

        if (compressed_file->original_size <= 0) {
            printf("A tomoritett fajl (%s) serult, nem sikerult beolvasni.\n", args.input_file);
            res = EINVAL;
            break;
        }

        *raw_data = malloc(compressed_file->original_size * sizeof(char));
        if (*raw_data == NULL) {
            printf("Nem sikerult lefoglalni a memoriat.\n");
            res = ENOMEM;
            break;
        }

        int decompress_result = decompress(compressed_file, *raw_data);
        if (decompress_result != 0) {
            printf("Nem sikerult a kitomorites.\n");
            res = EIO;
            break;
        }

        *raw_size = compressed_file->original_size;
        *is_directory = compressed_file->is_dir;
        *original_name = strdup(compressed_file->original_file);
        if (*original_name == NULL) {
            printf("Nem sikerult lefoglalni a memoriat.\n");
            res = ENOMEM;
            break;
        }
        break;
    }

    if (compressed_file != NULL) {
        free(compressed_file->file_name);
        free(compressed_file->original_file);
        free(compressed_file->huffman_tree);
        free(compressed_file->compressed_data);
        free(compressed_file);
    }

    if (res != 0 && raw_data != NULL && *raw_data != NULL) {
        free(*raw_data);
        *raw_data = NULL;
    }

    return res;
}

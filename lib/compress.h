#ifndef COMPRESS_H
#define COMPRESS_H

#include "data_types.h"
#include <stdbool.h>

int count_frequencies(const uint8_t *data, uint64_t data_len, uint64_t *frequencies);
Node* construct_tree(Node *nodes, uint64_t leaf_count);
Node construct_leaf(uint64_t frequency, uint8_t data);
Node construct_branch(Node *nodes, int left_index, int right_index);
void sort_nodes(Node *nodes, int len);
char* check_cache(uint8_t leaf, char **cache);
char* find_leaf(uint8_t leaf, Node *nodes, Node *root_node);
int compress(const uint8_t *original_data, uint64_t data_len, Node *nodes, Node *root_node, char** cache, Compressed_file *compressed_file);
char* generate_output_file(char *input_file);
int run_compression(Arguments args, const uint8_t *data, uint64_t data_len, uint64_t directory_size);

#endif

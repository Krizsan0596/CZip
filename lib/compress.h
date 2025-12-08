#ifndef COMPRESS_H
#define COMPRESS_H

#include "data_types.h"
#include <stdbool.h>

int count_frequencies(const char *data, long data_len, long *frequencies);
Node* construct_tree(Node *nodes, long leaf_count);
Node construct_leaf(long frequency, char data);
Node construct_branch(Node *nodes, int left_index, int right_index);
void sort_nodes(Node *nodes, int len);
char* check_cache(char leaf, char **cache);
char* find_leaf(char leaf, Node *nodes, Node *root_node);
int compress(const char *original_data, long data_len, Node *nodes, Node *root_node, char** cache, Compressed_file *compressed_file);
char* generate_output_file(char *input_file);
int run_compression(Arguments args, const char *data, long data_len, long directory_size);

#endif

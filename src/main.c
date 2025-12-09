#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "../lib/compatibility.h"
#include "../lib/file.h"
#include "../lib/compress.h"
#include "../lib/decompress.h"
#include "../lib/directory.h"
#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"

/*
 * Prints program usage instructions.
 * Called for help requests or invalid options.
 */
static void print_usage(const char *prog_name) {
    const char *usage =
        "Huffman encoder\n"
        "Usage: %s -c|-x [-o OUTPUT_FILE] INPUT_FILE\n"
        "\n"
        "Options:\n"
        "\t-c                        Compress\n"
        "\t-x                        Decompress\n"
        "\t-o OUTPUT_FILE            Set output file (optional).\n"
        "\t-h                        Show this guide.\n"
        "\t-f                        Overwrite OUTPUT_FILE without asking if it exists.\n"
        "\t-r                        Recursively compress a directory (only needed for compression).\n"
        "\t-P, --no-preserve-perms   When extracting, apply stored permissions even to existing directories.\n"
        "\tINPUT_FILE: Path to the file to compress or restore.\n"
        "\tThe -c and -x options are mutually exclusive.";

    printf(usage, prog_name);
}

/* 
 * Processes command-line options: only one mode is allowed; -o sets the output, -f controls overwrite.
 * The first non-flag argument becomes the input file.
 */
int parse_arguments(int argc, char* argv[], Arguments *args) {
    args->compress_mode = false;
    args->extract_mode = false;
    args->force = false;
    args->directory = false;
    args->no_preserve_perms = false;
    args->input_file = NULL;
    args->output_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--no-preserve-perms") == 0) {
                args->no_preserve_perms = true;
            } else {
                switch (argv[i][1]) {
                    case 'h':
                        print_usage(argv[0]);
                        return HELP_REQUESTED;
                    case 'c':
                        args->compress_mode = true;
                        break;
                    case 'x':
                        args->extract_mode = true;
                        break;
                    case 'f':
                        args->force = true;
                        break;
                    case 'r':
                        args->directory = true;
                        break;
                    case 'P':
                        args->no_preserve_perms = true;
                        break;
                    case 'o':
                        if (++i < argc) {
                            args->output_file = argv[i];
                        } else {
                            fprintf(stderr, "Provide the output file after the -o option.\n");
                            print_usage(argv[0]);
                            return EINVAL;
                        }
                        break;
                    default:
                        fprintf(stderr, "Unknown option: %s\n", argv[i]);
                        print_usage(argv[0]);
                        return EINVAL;
                }
            }
        } else {
            if (args->input_file == NULL) {
                args->input_file = argv[i];
            } else {
                fprintf(stderr, "Multiple input files were provided.\n");
                print_usage(argv[0]);
                return EINVAL;
            }
        }
    }

    /*
     * Ensure an input file was provided, then verify it exists.
     */
    if (args->input_file == NULL) {
        fprintf(stderr, "No input file was provided.\n");
        print_usage(argv[0]);
        return EINVAL;
    }
    
    if (access(args->input_file, F_OK) != 0) {
        fprintf(stderr, "The file (%s) was not found.\n", args->input_file);
        print_usage(argv[0]);
        return FILE_READ_ERROR;
    }

    if (args->compress_mode && args->extract_mode) {
        fprintf(stderr, "The -c and -x options are mutually exclusive.\n");
        print_usage(argv[0]);
        return EINVAL;
    }

    return SUCCESS;
}

/*
 * After processing options, starts compression or decompression and returns the error code.
 */
int main(int argc, char* argv[]){
    Arguments args;
    int parse_result = parse_arguments(argc, argv, &args);
    if (parse_result == HELP_REQUESTED) {
        return SUCCESS;
    }
    if (parse_result == FILE_READ_ERROR) {
        return ENOENT;
    }
    if (parse_result != SUCCESS) {
        return parse_result;
    }

    /* Verify that -r truly points to a directory, or disable it if misused. */
    if (args.directory) {
        struct stat st;
        int ret = stat(args.input_file, &st);
        if (ret != 0) {
            fprintf(stderr, "Failed to check the directory.\n");
            return ret;
        }
        else if (S_ISREG(st.st_mode)) args.directory = false;
    }
    else {
        struct stat st;
        int ret = stat(args.input_file, &st);
        if (ret != 0) {
            fprintf(stderr, "Failed to check the file.\n");
            return ret;
        }
        else if (S_ISDIR(st.st_mode)) {
            fprintf(stderr, "The program will not compress a directory without the -r option.\n");
            print_usage(argv[0]);
            return EISDIR;
        }
    }
    
    if (args.compress_mode) {
        const char *data = NULL;
        char *allocated_data = NULL;
        long data_len = 0;
        long directory_size = 0;
        int directory_size_int = 0;
        FILE *temp_file = NULL;
        bool use_mmap = false;

        if (args.directory) {
            temp_file = prepare_directory(args.input_file, &directory_size_int);
            if (temp_file == NULL) {
                fprintf(stderr, "Failed to prepare the directory.\n");
                return FILE_WRITE_ERROR;
            }
            directory_size = directory_size_int;
            int read_res = read_from_file(temp_file, &allocated_data);
            fclose(temp_file);
            if (read_res < 0) {
                if (read_res == MALLOC_ERROR) {
                    fprintf(stderr, "Failed to allocate memory.\n");
                } else {
                    fprintf(stderr, "Failed to read the serialized data.\n");
                }
                return read_res;
            }
            data = allocated_data;
            data_len = read_res;
        } else {
            int read_res = read_raw(args.input_file, &data);
            if (read_res < 0) {
                if (read_res == EMPTY_FILE) {
                    fprintf(stderr, "The file (%s) is empty.\n", args.input_file);
                } else if (read_res == MALLOC_ERROR) {
                    fprintf(stderr, "Failed to allocate memory.\n");
                } else if (read_res == FILE_READ_ERROR) {
                    fprintf(stderr, "Failed to read the file (%s).\n", args.input_file);
                } else {
                    fprintf(stderr, "Failed to open the file (%s).\n", args.input_file);
                }
                return read_res;
            }
            data_len = read_res;
            directory_size = data_len;
            use_mmap = true;
        }

        int compress_res = run_compression(args, data, data_len, directory_size);
        if (use_mmap) {
            munmap((void*)data, data_len);
        } else {
            free(allocated_data);
        }
        return compress_res;
    } else if (args.extract_mode) {
        char *raw_data = NULL;
        long raw_size = 0;
        bool is_dir = false;
        char *original_name = NULL;

        int decomp_res = run_decompression(args, &raw_data, &raw_size, &is_dir, &original_name);
        if (decomp_res != 0) {
            if (is_dir && raw_data != NULL) free(raw_data);
            free(original_name);
            return decomp_res;
        }

        int res = 0;
        if (is_dir) {
            FILE *temp_file = tmpfile();
            if (temp_file == NULL) {
                fprintf(stderr, "Failed to create temporary file.\n");
                free(raw_data);
                free(original_name);
                return FILE_WRITE_ERROR;
            }
            if (fwrite(raw_data, 1, raw_size, temp_file) != (size_t)raw_size) {
                fprintf(stderr, "Failed to write the serialized data.\n");
                fclose(temp_file);
                free(raw_data);
                free(original_name);
                return FILE_WRITE_ERROR;
            }
            res = restore_directory(temp_file, args.output_file, args.force, args.no_preserve_perms);
            fclose(temp_file);
            if (res < 0) {
                if (res == FILE_READ_ERROR) {
                    fprintf(stderr, "Failed to read the serialized data.\n");
                } else if (res == MALLOC_ERROR) {
                    fprintf(stderr, "Failed to allocate memory.\n");
                } else if (res == MKDIR_ERROR) {
                    fprintf(stderr, "Failed to create a directory.\n");
                } else if (res == FILE_WRITE_ERROR) {
                    fprintf(stderr, "Failed to write a file.\n");
                } else {
                    fprintf(stderr, "Failed to restore the directory.\n");
                }
            }
            free(raw_data);
        }

        free(original_name);
        return res;
    }
    else {
        fprintf(stderr, "You must specify one mode (-c or -x).\n");
        print_usage(argv[0]);
        return EINVAL;
    }
}

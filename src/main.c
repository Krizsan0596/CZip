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

/*
 * Prints program usage instructions.
 * Called for help requests or invalid options.
 */
static void print_usage(const char *prog_name) {
    printf(PROGRAM_USAGE_TEXT, prog_name);
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
                            fputs("Provide the output file after the -o option.\n", stderr);
                            print_usage(argv[0]);
                            return EINVAL;
                        }
                        break;
                    default:
                        fputs("Unknown option: ", stderr);
                        fputs(argv[i], stderr);
                        fputs("\n", stderr);
                        print_usage(argv[0]);
                        return EINVAL;
                }
            }
        } else {
            if (args->input_file == NULL) {
                args->input_file = argv[i];
            } else {
                fputs("Multiple input files were provided.\n", stderr);
                print_usage(argv[0]);
                return EINVAL;
            }
        }
    }

    /*
     * Ensure an input file was provided, then verify it exists.
     */
    if (args->input_file == NULL) {
        fputs("No input file was provided.\n", stderr);
        print_usage(argv[0]);
        return EINVAL;
    }
    
    if (access(args->input_file, F_OK) != 0) {
        fputs("The file (", stderr);
        fputs(args->input_file, stderr);
        fputs(") was not found.\n", stderr);
        print_usage(argv[0]);
        return FILE_READ_ERROR;
    }

    if (args->compress_mode && args->extract_mode) {
        fputs("The -c and -x options are mutually exclusive.\n", stderr);
        print_usage(argv[0]);
        return EINVAL;
    }

    return SUCCESS;
}

/*
 * After processing options, starts compression or decompression and returns the error code.
 */
int main(int argc, char* argv[]) {
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
            fputs("Failed to check the directory.\n", stderr);
            return ret;
        }
        else if (S_ISREG(st.st_mode)) args.directory = false;
    }
    else {
        struct stat st;
        int ret = stat(args.input_file, &st);
        if (ret != 0) {
            fputs("Failed to check the file.\n", stderr);
            return ret;
        }
        else if (S_ISDIR(st.st_mode)) {
            fputs("The program will not compress a directory without the -r option.\n", stderr);
            print_usage(argv[0]);
            return EISDIR;
        }
    }
    
    if (args.compress_mode) {
        const uint8_t *data = NULL;
        uint8_t *allocated_data = NULL;
        uint64_t data_len = 0;
        uint64_t directory_size = 0;
        int directory_size_int = 0;
        int fd = -1;
        bool use_mmap = false;

        if (args.directory) {
            uint64_t total_size = 0;
            fd = prepare_directory(args.input_file, &directory_size_int, &total_size);
            if (fd < 0) {
                // prepare_directory already prints errors
                return FILE_WRITE_ERROR;
            }
            directory_size = directory_size_int;
            
            int64_t read_res = read_raw_from_fd(fd, &data);
            close(fd);            

            if (read_res < 0) {
                if (read_res == MALLOC_ERROR) {
                    fputs("Failed to allocate memory.\n", stderr);
                } else {
                    fputs("Failed to read the serialized data.\n", stderr);
                }
                return read_res;
            }
            data_len = read_res;
            use_mmap = true;
        } else {
            int64_t read_res = read_raw(args.input_file, &data);
            if (read_res < 0) {
                if (read_res == EMPTY_FILE) {
                    fputs("The file (", stderr);
                    fputs(args.input_file, stderr);
                    fputs(") is empty.\n", stderr);
                } else if (read_res == MALLOC_ERROR) {
                    fputs("Failed to allocate memory.\n", stderr);
                } else if (read_res == FILE_READ_ERROR) {
                    fputs("Failed to read the file (", stderr);
                    fputs(args.input_file, stderr);
                    fputs(").\n", stderr);
                } else {
                    fputs("Failed to open the file (", stderr);
                    fputs(args.input_file, stderr);
                    fputs(").\n", stderr);
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
        uint8_t *raw_data = NULL;
        uint64_t raw_size = 0;
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
            int fd = create_mmapable_tmpfile((size_t)raw_size);
            if (fd < 0) {
                fputs("Failed to create temporary file.\n", stderr);
                free(raw_data);
                free(original_name);
                return FILE_WRITE_ERROR;
            }
            
            uint8_t *map = NULL;
            int64_t map_res = write_raw_to_fd(fd, &map, raw_size);
            if (map_res < 0) {
                fputs("Failed to map temporary file for writing.\n", stderr);
                close(fd);
                free(raw_data);
                free(original_name);
                return FILE_WRITE_ERROR;
            }
            
            memcpy(map, raw_data, raw_size);
            
            res = restore_directory(map, (int64_t)raw_size, args.output_file, args.force, args.no_preserve_perms);
            
            munmap(map, raw_size);
            close(fd);

            free(raw_data);
        }

        free(original_name);
        return res;
    }
    else {
        fputs("You must specify one mode (-c or -x).\n", stderr);
        print_usage(argv[0]);
        return EINVAL;
    }
}

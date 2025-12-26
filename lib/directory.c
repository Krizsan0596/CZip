#include "directory.h"
#include "data_types.h"
#include "file.h"
#include "compatibility.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

/*
 * Recursively walks the directory and serializes every entry.
 * If out_data is NULL, it only calculates and returns the total serialized size.
 * If out_data is not NULL, it serializes entries into the buffer.
 */
int64_t archive_directory(char *path, int *archive_size, int64_t *data_size, uint8_t *out_data) {
    DIR *directory = NULL;
    int64_t dir_size = 0;
    int64_t result = 0;
    char *newpath = NULL;
    Directory_item current_item = {0};
    uint8_t *current_ptr = out_data;
    
    while (true) {
        /* On the first call, add the root directory to the archive so relative paths are preserved. */
        if (*archive_size == 0) {
            struct stat root_st;
            if (stat(path, &root_st) != 0) {
                result = DIRECTORY_ERROR;
                break;
            }
            Directory_item root = {0};
            root.is_dir = true;
            root.dir_path = strdup(path);
            root.perms = root_st.st_mode & 0777;
            if (root.dir_path == NULL) {
                result = MALLOC_ERROR;
                break;
            }
            (*archive_size)++;
            int64_t bytes_written = serialize_item(&root, current_ptr);
            if (bytes_written < 0) {
                result = bytes_written;
                free(root.dir_path);
                break;
            }
            *data_size += bytes_written;
            if (current_ptr) current_ptr += bytes_written;
            free(root.dir_path);
        }

        directory = opendir(path);
        if (directory == NULL) {
            result = DIRECTORY_ERROR;
            break;
        }

        while (true) {
            struct dirent *dir = readdir(directory);
            if (dir == NULL) break;
            else if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
            
            newpath = malloc(strlen(path) + strlen(dir->d_name) + 2);
            if (newpath == NULL) {
                result = MALLOC_ERROR;
                break;
            }
            snprintf(newpath, strlen(path) + strlen(dir->d_name) + 2, "%s/%s", path, dir->d_name);

            struct stat st;
            if (stat(newpath, &st) != 0) {
                free(newpath);
                newpath = NULL;
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                Directory_item subdir = {0};
                subdir.is_dir = true;
                subdir.dir_path = strdup(newpath);
                subdir.perms = st.st_mode & 0777;
                if (subdir.dir_path == NULL) {
                    result = MALLOC_ERROR;
                    current_item = subdir;
                    break;
                }
                (*archive_size)++;
                int64_t bytes_written = serialize_item(&subdir, current_ptr);
                if (bytes_written < 0) {
                    result = bytes_written;
                    free(subdir.dir_path);
                    break;
                }
                *data_size += bytes_written;
                if (current_ptr) current_ptr += bytes_written;
                free(subdir.dir_path);
                int64_t subdir_size = archive_directory(newpath, archive_size, data_size, current_ptr);
                if (subdir_size < 0) {
                    result = subdir_size;
                    break;
                }
                dir_size += subdir_size;
                if (current_ptr) {
                    current_ptr = out_data + *data_size;
                }
            } 
            else if (S_ISREG(st.st_mode)) {
                Directory_item file = {0};
                file.is_dir = false;
                file.file_path = strdup(newpath);
                if (file.file_path == NULL) {
                    result = MALLOC_ERROR;
                    current_item = file;
                    break;
                }
                file.file_size = read_raw(newpath, (const uint8_t**)&file.file_data);
                if (file.file_size < 0) {
                    if (file.file_size == EMPTY_FILE) {
                        /* Empty files are valid - include them with size 0 */
                        file.file_size = 0;
                        file.file_data = NULL;
                    } else {
                        result = FILE_READ_ERROR;
                        current_item = file;
                        break;
                    }
                }
                dir_size += file.file_size;
                (*archive_size)++;
                int64_t bytes_written = serialize_item(&file, current_ptr);
                if (bytes_written < 0) {
                    result = bytes_written;
                    current_item = file;
                    break;
                }
                *data_size += bytes_written;
                if (current_ptr) current_ptr += bytes_written;
                free(file.file_path);
                if (file.file_data != NULL) munmap(file.file_data, file.file_size);
            }
            free(newpath);
            newpath = NULL;
        }
        
        if (result < 0) break;
        result = dir_size;
        break;
    }
    
    if (result < 0) {
        if (current_item.is_dir) {
            free(current_item.dir_path);
        } else {
            free(current_item.file_path);
            if (current_item.file_data != NULL) munmap(current_item.file_data, current_item.file_size);
        }
    }
    free(newpath);
    if (directory != NULL) closedir(directory);
    return result;
}

/*
 * Serializes an archived directory element into a buffer.
 * If out_data is NULL, it only calculates and returns the item's serialized size.
 */
int64_t serialize_item(Directory_item *item, uint8_t *out_data) {
    size_t path_len = item->is_dir ? (strlen(item->dir_path) + 1) : (strlen(item->file_path) + 1);
    int64_t item_size = (int64_t)sizeof(bool) + ((item->is_dir) ? (int64_t)(sizeof(int) + path_len) : (int64_t)(sizeof(size_t) + path_len + item->file_size));
    int64_t total_size = (int64_t)sizeof(int64_t) + item_size;

    if (out_data == NULL) return total_size;

    uint8_t *ptr = out_data;
    memcpy(ptr, &item_size, sizeof(int64_t));
    ptr += sizeof(int64_t);

    memcpy(ptr, &item->is_dir, sizeof(bool));
    ptr += sizeof(bool);

    if (item->is_dir) {
        memcpy(ptr, &item->perms, sizeof(int));
        ptr += sizeof(int);

        convert_path(item->dir_path);
        memcpy(ptr, item->dir_path, path_len);
    }
    else {
        memcpy(ptr, &item->file_size, sizeof(size_t));
        ptr += sizeof(size_t);

        convert_path(item->file_path);
        memcpy(ptr, item->file_path, path_len);
        ptr += path_len;

        if (item->file_size > 0) {
            memcpy(ptr, item->file_data, item->file_size);
        }
    }
    return total_size;
}


/*
 * Extracts the archived directory to the given path, creating directories and files as needed.
 */
int extract_directory(char *path, Directory_item *item, bool force, bool no_preserve_perms) {
    if (path == NULL) path = ".";
    char *item_path = item->is_dir ? item->dir_path : item->file_path;
    char *full_path = malloc(strlen(path) + strlen(item_path) + 2);
    if (full_path == NULL) return MALLOC_ERROR;

    /* If the user provided an output directory, start building the structure there. */
    snprintf(full_path, strlen(path) + strlen(item_path) + 2, "%s/%s", path, item_path);
    if (item->is_dir) {
       int ret = mkdir(full_path, item->perms);
       if (ret != 0 && errno != EEXIST) {
           free(full_path);
           return MKDIR_ERROR;
       }
       #ifndef _WIN32
       if (no_preserve_perms && errno == EEXIST) {
           if (chmod(full_path, item->perms) != 0) {
               free(full_path);
               return MKDIR_ERROR;
           }
       }
       #endif
    }
    else {
        if (item->file_size == 0) {
            FILE *file = fopen(full_path, "wb");
            if (file == NULL) {
                free(full_path);
                return FILE_WRITE_ERROR;
            }
            fclose(file);
        } else if (item->file_size > 0) {
            if (item->file_data == NULL) {
                free(full_path);
                return FILE_WRITE_ERROR;
            }
            uint8_t *mmap_ptr = NULL;
            int ret = write_raw(full_path, &mmap_ptr, item->file_size, force);
            if (ret < 0) {
                free(full_path);
                return FILE_WRITE_ERROR;
            }
            memcpy(mmap_ptr, item->file_data, item->file_size);
            if (msync(mmap_ptr, item->file_size, MS_SYNC) == -1) {
                munmap(mmap_ptr, item->file_size);
                free(full_path);
                return FILE_WRITE_ERROR;
            }
            munmap(mmap_ptr, item->file_size);
        } else {
            free(full_path);
            return FILE_READ_ERROR;
        }
    }
    free(full_path);
    return SUCCESS;
}

/*
 * Reconstructs the archive array from the serialized buffer.
 */
int64_t deserialize_item(Directory_item *item, const uint8_t *in_data, int64_t remaining_size) {
    if (remaining_size < (int64_t)sizeof(int64_t)) return 0;
    
    int64_t item_size;
    const uint8_t *ptr = in_data;
    memcpy(&item_size, ptr, sizeof(int64_t));
    ptr += sizeof(int64_t);

    if (remaining_size < (int64_t)sizeof(int64_t) + item_size) return FILE_READ_ERROR;

    memcpy(&item->is_dir, ptr, sizeof(bool));
    ptr += sizeof(bool);

    if (item->is_dir) {
        memcpy(&item->perms, ptr, sizeof(int));
        ptr += sizeof(int);

        size_t path_len = item_size - sizeof(bool) - sizeof(int);
        item->dir_path = malloc(path_len);
        if (item->dir_path == NULL) return MALLOC_ERROR;
        memcpy(item->dir_path, ptr, path_len);
    }
    else {
        memcpy(&item->file_size, ptr, sizeof(size_t));
        ptr += sizeof(size_t);

        size_t path_len = item_size - sizeof(bool) - sizeof(size_t) - item->file_size;
        item->file_path = malloc(path_len);
        if (item->file_path == NULL) return MALLOC_ERROR;
        memcpy(item->file_path, ptr, path_len);
        ptr += path_len;

        if (item->file_size > 0) {
            item->file_data = malloc(item->file_size);
            if (item->file_data == NULL) {
                return MALLOC_ERROR;
            }
            memcpy(item->file_data, ptr, item->file_size);
        } else {
            item->file_data = NULL;
        }
    }
    return (int64_t)sizeof(int64_t) + item_size;
}

/*
 * Prepares a directory for compression by serializing it into a memory-mapped temporary file.
 * Returns the file descriptor on success or a negative error code.
 */
int prepare_directory(char *input_file, int *directory_size, uint64_t *total_size) {
    char current_path[PATH_MAX];
    char *sep = strrchr(input_file, '/');
#ifdef _WIN32
    char *sep_win = strrchr(input_file, '\\');
    if (sep == NULL || (sep_win != NULL && sep_win > sep)) {
        sep = sep_win;
    }
#endif
    char *parent_dir = NULL;
    char *file_name = NULL;
    int archive_size = 0;
    int64_t data_len = 0;
    int fd = -1;
    
    while (true) {
        if (getcwd(current_path, sizeof(current_path)) == NULL) {
            fputs("Failed to store the current path.\n", stderr);
            break;
        }
        
        if (sep != NULL) {
            if (sep == input_file) {
                parent_dir = strdup("/");
            }
            else {
                int parent_dir_len = sep - input_file;
                parent_dir = malloc(parent_dir_len + 1);
                if (parent_dir != NULL) snprintf(parent_dir, parent_dir_len + 1, "%.*s", parent_dir_len, input_file);
            }
            if (parent_dir == NULL) break;
            file_name = strdup(sep + 1);
            if (file_name == NULL) break;
            if (chdir(parent_dir) != 0) {
                fputs("Failed to enter the directory.\n", stderr);
                break;
            }
        }
        
        // Step 1: Calculate size
        int64_t dir_size = archive_directory((file_name != NULL) ? file_name : input_file, &archive_size, &data_len, NULL);
        if (dir_size < 0) {
            if (dir_size == MALLOC_ERROR) {
                fputs("Failed to allocate memory while archiving the directory.\n", stderr);
            } else if (dir_size == DIRECTORY_OPEN_ERROR) {
                fputs("Failed to open the directory.\n", stderr);
            } else if (dir_size == FILE_READ_ERROR) {
                fputs("Failed to read a file from the directory.\n", stderr);
            } else {
                fputs("Failed to archive the directory.\n", stderr);
            }
            if (sep != NULL) chdir(current_path);
            fd = (int)dir_size;
            break;
        }

        // Step 2: Create mmapable tmpfile
        fd = create_mmapable_tmpfile((size_t)data_len);
        if (fd < 0) {
            fputs("Failed to create the temp file.\n", stderr);
            if (sep != NULL) chdir(current_path);
            break;
        }

        // Step 3: Map and serialize
        uint8_t *map = NULL;
        int64_t map_res = write_raw_to_fd(fd, &map, (uint64_t)data_len);
        if (map_res < 0) {
            fputs("Failed to map the temp file for writing.\n", stderr);
            close(fd);
            fd = (int)map_res;
            if (sep != NULL) chdir(current_path);
            break;
        }

        archive_size = 0;
        int64_t final_data_len = 0;
        archive_directory((file_name != NULL) ? file_name : input_file, &archive_size, &final_data_len, map);
        
        if (msync(map, (size_t)data_len, MS_SYNC) == -1) {
            fputs("Failed to sync the temp file.\n", stderr);
            munmap(map, (size_t)data_len);
            close(fd);
            fd = FILE_WRITE_ERROR;
            if (sep != NULL) chdir(current_path);
            break;
        }
        munmap(map, (size_t)data_len);

        *directory_size = (int)dir_size;
        *total_size = (uint64_t)data_len;
        
        if (sep != NULL) {
            if (chdir(current_path) != 0) {
                fputs("Failed to exit the directory.\n", stderr);
                close(fd);
                fd = DIRECTORY_ERROR;
                break;
            }
        }
        break;
    }
    
    free(parent_dir);
    free(file_name);
    return fd;
}

/*
 * Handles directory processing for extraction from a memory-mapped buffer.
 */
int restore_directory(const uint8_t *temp_data, int64_t temp_size, char *output_file, bool force, bool no_preserve_perms) {
    if (temp_data == NULL) {
        fputs("Invalid temp file data.\n", stderr);
        return FILE_READ_ERROR;
    }
    
    if (output_file != NULL) {
        if (mkdir(output_file, DIR_MODE) != 0 && errno != EEXIST) {
            fputs("Failed to create the output directory.\n", stderr);
            return MKDIR_ERROR;
        }
    }
    
    int64_t offset = 0;
    while (offset < temp_size) {
        Directory_item item = {0};
        int64_t bytes_read = deserialize_item(&item, temp_data + offset, temp_size - offset);
        if (bytes_read < 0) {
             if (bytes_read == MALLOC_ERROR) {
                fputs("Failed to allocate memory while reading.\n", stderr);
            } else {
                fputs("Failed to read the compressed directory.\n", stderr);
            }
            if (item.is_dir) free(item.dir_path);
            else {
                free(item.file_path);
                free(item.file_data);
            }
            return (int)bytes_read;
        }
        if (bytes_read == 0) {
            if (item.is_dir) free(item.dir_path);
            else {
                free(item.file_path);
                free(item.file_data);
            }
            break;
        }
        
        int ret = extract_directory(output_file, &item, force, no_preserve_perms);
        
        if (item.is_dir) free(item.dir_path);
        else {
            free(item.file_path);
            free(item.file_data);
        }
        
        if (ret != 0) {
            if (ret == MKDIR_ERROR) {
                fputs("Failed to create a directory during extraction.\n", stderr);
            } else if (ret == FILE_WRITE_ERROR) {
                fputs("Failed to write a file during extraction.\n", stderr);
            } else if (ret == MALLOC_ERROR) {
                fputs("Failed to allocate memory.\n", stderr);
            } else {
                fputs("Failed to extract the directory.\n", stderr);
            }
            return ret;
        }
        offset += bytes_read;
    }
    return SUCCESS;
}

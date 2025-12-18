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
 * Recursively walks the directory and serializes every entry into the provided stream.
 * Returns the total size of all file payloads on success or a negative code on failure.
 */
long archive_directory(char *path, int *archive_size, long *data_size, FILE *out_file) {
    DIR *directory = NULL;
    long dir_size = 0;
    long result = 0;
    char *newpath = NULL;
    Directory_item current_item = {0};
    
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
            long bytes_written = serialize_item(&root, out_file);
            if (bytes_written < 0) {
                result = bytes_written;
                free(root.dir_path);
                break;
            }
            *data_size += bytes_written;
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
                long bytes_written = serialize_item(&subdir, out_file);
                if (bytes_written < 0) {
                    result = bytes_written;
                    free(subdir.dir_path);
                    break;
                }
                *data_size += bytes_written;
                free(subdir.dir_path);
                long subdir_size = archive_directory(newpath, archive_size, data_size, out_file);
                if (subdir_size < 0) {
                    result = subdir_size;
                    break;
                }
                dir_size += subdir_size;
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
                long bytes_written = serialize_item(&file, out_file);
                if (bytes_written < 0) {
                    result = bytes_written;
                    current_item = file;
                    break;
                }
                *data_size += bytes_written;
                free(file.file_path);
                if (file.file_data != NULL) munmap((void*)file.file_data, file.file_size);
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
            if (current_item.file_data != NULL) munmap((void*)current_item.file_data, current_item.file_size);
        }
    }
    free(newpath);
    if (directory != NULL) closedir(directory);
    return result;
}

/*
 * Serializes an archived directory element into a buffer.
 * Returns the buffer size on success or a negative code on failure.
 */
long serialize_item(Directory_item *item, FILE *out_file) {
    long data_size = 0;
    long item_size = sizeof(bool) + ((item->is_dir) ? (strlen(item->dir_path) + 1 + sizeof(int)) : (sizeof(size_t) + strlen(item->file_path) + 1 + item->file_size));
    
    if (fwrite(&item_size, sizeof(long), 1, out_file) != 1) {
        return FILE_WRITE_ERROR;
    }
    data_size += sizeof(long);
    
    if (fwrite(&item->is_dir, sizeof(bool), 1, out_file) != 1) {
        return FILE_WRITE_ERROR;
    }
    data_size += sizeof(bool);
    
    if (item->is_dir) {
        if (fwrite(&item->perms, sizeof(int), 1, out_file) != 1) {
            return FILE_WRITE_ERROR;
        }
        data_size += sizeof(int);
        
        convert_path(item->dir_path);
        size_t path_len = strlen(item->dir_path) + 1;
        if (fwrite(item->dir_path, sizeof(char), path_len, out_file) != path_len) {
            return FILE_WRITE_ERROR;
        }
        data_size += path_len;
    }
    else {
        if (fwrite(&item->file_size, sizeof(size_t), 1, out_file) != 1) {
            return FILE_WRITE_ERROR;
        }
        data_size += sizeof(size_t);
        
        convert_path(item->file_path);
        size_t path_len = strlen(item->file_path) + 1;
        if (fwrite(item->file_path, sizeof(char), path_len, out_file) != path_len) {
            return FILE_WRITE_ERROR;
        }
        data_size += path_len;
        
        if (item->file_size > 0) {
            if (fwrite(item->file_data, sizeof(uint8_t), item->file_size, out_file) != (size_t)item->file_size) {
                return FILE_WRITE_ERROR;
            }
            data_size += item->file_size;
        }
    }
    return data_size;
}


/*
 * Extracts the archived directory to the given path, creating directories and files as needed.
 * Returns 0 on success or a negative code on failure.
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
 * Returns the archive size on success or a negative code on failure.
 */
long deserialize_item(Directory_item *item, FILE *in_file) {
    long archive_size;
    long read_size = 0;
    if (fread(&archive_size, sizeof(long), 1, in_file) != 1) {
        if (feof(in_file)) return 0;
        return FILE_READ_ERROR;
    }
    read_size += sizeof(long);
    
    if (fread(&item->is_dir, sizeof(bool), 1, in_file) != 1) {
        return FILE_READ_ERROR;
    }
    read_size += sizeof(bool);
    
    if (item->is_dir) {
        if (fread(&item->perms, sizeof(int), 1, in_file) != 1) {
            return FILE_READ_ERROR;
        }
        read_size += sizeof(int);
        
        size_t path_len = archive_size - sizeof(bool) - sizeof(int);
        item->dir_path = malloc(sizeof(char) * path_len);
        if (item->dir_path == NULL) return MALLOC_ERROR;
        
        if (fread(item->dir_path, sizeof(char), path_len, in_file) != (size_t)path_len) {
            free(item->dir_path);
            item->dir_path = NULL;
            return FILE_READ_ERROR;
        }
        read_size += sizeof(char) * path_len;
    }
    else {
        if (fread(&item->file_size, sizeof(size_t), 1, in_file) != 1) {
            return FILE_READ_ERROR;
        }
        read_size += sizeof(size_t);
        
        size_t path_len = archive_size - sizeof(bool) - sizeof(size_t) - item->file_size;
        item->file_path = malloc(path_len);
        if (item->file_path == NULL) return MALLOC_ERROR;
        
        if (fread(item->file_path, sizeof(char), path_len, in_file) != (size_t)path_len) {
            free(item->file_path);
            item->file_path = NULL;
            return FILE_READ_ERROR;
        }
        read_size += sizeof(char) * path_len;
        
        if (item->file_size > 0) {
            item->file_data = malloc(item->file_size);
            if (item->file_data == NULL) {
                free(item->file_path);
                item->file_path = NULL;
                return MALLOC_ERROR;
            }
            
            if (fread(item->file_data, sizeof(uint8_t), item->file_size, in_file) != (size_t)item->file_size) {
                free(item->file_path);
                free(item->file_data);
                item->file_path = NULL;
                item->file_data = NULL;
                return FILE_READ_ERROR;
            }
            read_size += sizeof(uint8_t) * item->file_size;
        } else {
            item->file_data = NULL;
        }
    }
    if (read_size != archive_size + sizeof(long)) {
        if (item->is_dir) {
            free(item->dir_path);
            item->dir_path = NULL;
        } else {
            free(item->file_path);
            free(item->file_data);
            item->file_path = NULL;
            item->file_data = NULL;
        }
        return FILE_READ_ERROR;
    }
    return archive_size + sizeof(long);
}

/*
 * Prepares a directory for compression.
 * Walks the directory, archives it, and serializes the data into a temporary file.
 * Returns a FILE* on success or NULL on failure.
 */
FILE* prepare_directory(char *input_file, int *directory_size) {
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
    long data_len = 0;
    FILE *temp_file = NULL;
    
    while (true) {
        if (getcwd(current_path, sizeof(current_path)) == NULL) {
            fputs("Failed to store the current path.\n", stderr);
            break;
        }
        
        /* For external paths, switch to the parent directory so stored paths remain relative. */
        if (sep != NULL) {
            if (sep == input_file) {
                parent_dir = strdup("/");
                if (parent_dir == NULL) {
                    break;
                }
            }
            else {
                int parent_dir_len = sep - input_file;
                parent_dir = malloc(parent_dir_len + 1);
                if (parent_dir == NULL) {
                    break;
                }
                snprintf(parent_dir, parent_dir_len + 1, "%.*s", parent_dir_len, input_file);
            }
            file_name = strdup(sep + 1);
            if (file_name == NULL) {
                break;
            }
            if (chdir(parent_dir) != 0) {
                fputs("Failed to enter the directory.\n", stderr);
                break;
            }
        }
        
        /* Create temporary file using tmpfile() */
        temp_file = tmpfile();
        if (temp_file == NULL) {
            fputs("Failed to create the temp file.\n", stderr);
            if (sep != NULL) {
                if (chdir(current_path) != 0) {
                    fputs("Failed to exit the directory.\n", stderr);
                }
            }
            break;
        }
        
        long dir_size = archive_directory((file_name != NULL) ? file_name : input_file, &archive_size, &data_len, temp_file);
        
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
            /* Return to the original directory even when an error occurs. */
            if (sep != NULL) {
                if (chdir(current_path) != 0) {
                    fputs("Failed to exit the directory.\n", stderr);
                }
            }
            fclose(temp_file);
            temp_file = NULL;
            break;
        }
        
        *directory_size = (int)dir_size;
        
        /* Return to the original directory. */
        if (sep != NULL) {
            if (chdir(current_path) != 0) {
                fputs("Failed to exit the directory.\n", stderr);
                fclose(temp_file);
                temp_file = NULL;
                break;
            }
        }
        
        /* Rewind the file pointer to the beginning for reading */
        rewind(temp_file);
        break;
    }
    
    free(parent_dir);
    free(file_name);
    
    return temp_file;
}

/*
 * Handles directory processing for extraction.
 * Deserializes and extracts the archived directories.
 * Returns 0 on success or a negative value on failure.
 */
int restore_directory(FILE *temp_file, char *output_file, bool force, bool no_preserve_perms) {
    int res = 0;
    Directory_item item = {0};
    
    while (true) {
        if (temp_file == NULL) {
            fputs("Invalid temp file.\n", stderr);
            res = FILE_READ_ERROR;
            break;
        }
        
        /* Rewind to the beginning of the temp file */
        rewind(temp_file);
        
        if (output_file != NULL) {
            if (mkdir(output_file, DIR_MODE) != 0 && errno != EEXIST) {
                fputs("Failed to create the output directory.\n", stderr);
                res = MKDIR_ERROR;
                break;
            }
        }
        
        while (true) {
            item = (Directory_item){0};
            long bytes_read = deserialize_item(&item, temp_file);
            if (bytes_read < 0) {
                if (bytes_read == MALLOC_ERROR) {
                    fputs("Failed to allocate memory while reading.\n", stderr);
                } else {
                    fputs("Failed to read the compressed directory.\n", stderr);
                }
                res = bytes_read;
                // Free any memory allocated in item before breaking
                if (item.is_dir) {
                    free(item.dir_path);
                } else {
                    free(item.file_path);
                    free(item.file_data);
                }
                break;
            }
            if (bytes_read == 0 || feof(temp_file)) break;
            
            int ret = extract_directory(output_file, &item, force, no_preserve_perms);
            
            if (item.is_dir) {
                free(item.dir_path);
            } else {
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
                res = ret;
                break;
            }
        }
        
        break;
    }
    
    return res;
}

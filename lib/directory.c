#include "directory.h"
#include "data_types.h"
#include "file.h"
#include "debugmalloc.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

/*
 * Rekurzivan bejarja a mappat es fajlonkent egy tombbe menti az adatokat.
 * Siker eseten a mappa meretet adja vissza bajtokban, hiba eseten negativ kodot.
 */
/* Static variable to store the base directory for temp file creation */
static char g_temp_file_base_dir[PATH_MAX] = "";

long archive_directory(char *path, int *archive_size, long *data_size, FILE *f) {
    DIR *directory = NULL;
    long dir_size = 0;
    long result = 0;
    char *newpath = NULL;
    Directory_item current_item = {0};
    bool is_root = (f == NULL);
    static char temp_file_path[PATH_MAX];
    
    if (is_root) {
        /* Use the base directory set by prepare_directory, or current directory if not set */
        const char *base_dir = (g_temp_file_base_dir[0] != '\0') ? g_temp_file_base_dir : ".";
        int path_len = snprintf(temp_file_path, sizeof(temp_file_path), "%s/%s", base_dir, SERIALIZED_TMP_FILE);
        if (path_len >= (int)sizeof(temp_file_path)) {
            return DIRECTORY_ERROR;
        }
        
        f = fopen(temp_file_path, "wb");
        if (f == NULL) {
            return FILE_WRITE_ERROR;
        }
    }
    
    while (true) {
        /* Az elso hivaskor felvesszuk a gyoker mappat az archivumba, hogy a relativ utak megmaradjanak. */
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
            long serialize_res = serialize_item(&root, f);
            if (serialize_res < 0) {
                free(root.dir_path);
                result = serialize_res;
                break;
            }
            *data_size += serialize_res;
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
            strcpy(newpath, path);
            strcat(newpath, "/");
            strcat(newpath, dir->d_name);

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
                long serialize_res = serialize_item(&subdir, f);
                free(subdir.dir_path);
                if (serialize_res < 0) {
                    result = serialize_res;
                    break;
                }
                *data_size += serialize_res;
                long subdir_size = archive_directory(newpath, archive_size, data_size, f);
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
                file.file_size = read_raw(newpath, &file.file_data);
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
                long serialize_res = serialize_item(&file, f);
                free(file.file_path);
                free(file.file_data);
                if (serialize_res < 0) {
                    result = serialize_res;
                    current_item = file;
                    break;
                }
                *data_size += serialize_res;
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
            free(current_item.file_data);
        }
    }
    free(newpath);
    if (directory != NULL) closedir(directory);
    if (is_root) {
        fclose(f);
        if (result < 0) {
            remove(temp_file_path);
        }
    }
    return result;
}

/*
 * Szerializalja az archivalt mappa elemet egy bufferbe.
 * Siker eseten a buffer meretet adja vissza, hiba eseten negativ kodot.
 */
long serialize_item(Directory_item *item, FILE *f) {
    long data_size = 0;
    long item_size = sizeof(bool) + ((item->is_dir) ? (strlen(item->dir_path) + 1 + sizeof(int)) : (sizeof(long) + strlen(item->file_path) + 1 + item->file_size));
    
    if (fwrite(&item_size, sizeof(long), 1, f) != 1) return FILE_WRITE_ERROR;
    data_size += sizeof(long);
    
    if (fwrite(&item->is_dir, sizeof(bool), 1, f) != 1) return FILE_WRITE_ERROR;
    data_size += sizeof(bool);
    
    if (item->is_dir) {
        if (fwrite(&item->perms, sizeof(int), 1, f) != 1) return FILE_WRITE_ERROR;
        data_size += sizeof(int);
        
        size_t path_len = strlen(item->dir_path) + 1;
        if (fwrite(item->dir_path, sizeof(char), path_len, f) != path_len) return FILE_WRITE_ERROR;
        data_size += path_len;
    }
    else {
        if (fwrite(&item->file_size, sizeof(long), 1, f) != 1) return FILE_WRITE_ERROR;
        data_size += sizeof(long);
        
        size_t path_len = strlen(item->file_path) + 1;
        if (fwrite(item->file_path, sizeof(char), path_len, f) != path_len) return FILE_WRITE_ERROR;
        data_size += path_len;
        
        if (item->file_size > 0 && fwrite(item->file_data, sizeof(char), item->file_size, f) != (size_t)item->file_size) return FILE_WRITE_ERROR;
        data_size += item->file_size;
    }
    return data_size;
}


/*
 * Kicsomagolja az archivalt mappat a megadott utvonalra, letrehozza a mappakat es fajlokat.
 * Siker eseten 0-t ad vissza, hiba eseten negativ kodot.
 */
int extract_directory(char *path, Directory_item *item, bool force, bool no_preserve_perms) {
    if (path == NULL) path = ".";
    char *item_path = item->is_dir ? item->dir_path : item->file_path;
    char *full_path = malloc(strlen(path) + strlen(item_path) + 2);
    if (full_path == NULL) return MALLOC_ERROR;

    /* Ha a felhasznalo adott kimeneti mappat, akkor ott kezdjuk a mappa felepiteset. */
    strcpy(full_path, path);
    strcat(full_path, "/");
    strcat(full_path, item_path);
    if (item->is_dir) {
       int ret = mkdir(full_path, item->perms);
       if (ret != 0 && errno != EEXIST) {
           free(full_path);
           return MKDIR_ERROR;
       }
       if (no_preserve_perms && errno == EEXIST) {
           if (chmod(full_path, item->perms) != 0) {
               free(full_path);
               return MKDIR_ERROR;
           }
       }
    }
    else {
        if (item->file_size == 0) {
            FILE *f = fopen(full_path, "wb");
            if (f == NULL) {
                free(full_path);
                return FILE_WRITE_ERROR;
            }
            fclose(f);
        } else {
            int ret = write_raw(full_path, item->file_data, item->file_size, force);
            if (ret < 0) {
                free(full_path);
                return FILE_WRITE_ERROR;
            }
        }
    }
    free(full_path);
    return SUCCESS;
}

/*
 * Visszaalakitja a szerializalt bufferbol az archivum tombot.
 * Siker eseten az archivum meretet adja vissza, hiba eseten negativ kodot.
 */
long deserialize_item(Directory_item *item, FILE *f) {
    long archive_size;
    long read_size = 0;
    
    if (fread(&archive_size, sizeof(long), 1, f) != 1) {
        if (feof(f)) return 0;
        return FILE_READ_ERROR;
    }
    read_size += sizeof(long);
    
    if (fread(&item->is_dir, sizeof(bool), 1, f) != 1) {
        return FILE_READ_ERROR;
    }
    read_size += sizeof(bool);
    
    if (item->is_dir) {
        if (fread(&item->perms, sizeof(int), 1, f) != 1) {
            return FILE_READ_ERROR;
        }
        read_size += sizeof(int);
        
        int path_len = archive_size - sizeof(bool) - sizeof(int);
        item->dir_path = malloc(sizeof(char) * path_len);
        if (item->dir_path == NULL) return MALLOC_ERROR;
        
        if (fread(item->dir_path, sizeof(char), path_len, f) != (size_t)path_len) {
            free(item->dir_path);
            item->dir_path = NULL;
            return FILE_READ_ERROR;
        }
        read_size += path_len;
    }
    else {
        if (fread(&item->file_size, sizeof(long), 1, f) != 1) {
            return FILE_READ_ERROR;
        }
        read_size += sizeof(long);
        
        int path_len = archive_size - sizeof(bool) - sizeof(long) - item->file_size;
        item->file_path = malloc(path_len);
        if (item->file_path == NULL) return MALLOC_ERROR;
        
        if (fread(item->file_path, sizeof(char), path_len, f) != (size_t)path_len) {
            free(item->file_path);
            item->file_path = NULL;
            return FILE_READ_ERROR;
        }
        read_size += path_len;
        
        if (item->file_size > 0) {
            item->file_data = malloc(item->file_size);
            if (item->file_data == NULL) {
                free(item->file_path);
                item->file_path = NULL;
                return MALLOC_ERROR;
            }
            
            if (fread(item->file_data, sizeof(char), item->file_size, f) != (size_t)item->file_size) {
                free(item->file_path);
                free(item->file_data);
                item->file_path = NULL;
                item->file_data = NULL;
                return FILE_READ_ERROR;
            }
            read_size += item->file_size;
        } else {
            item->file_data = NULL;
        }
    }
    
    if (read_size != archive_size + sizeof(long)) {
        // Clean up allocated memory before returning error
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
 * Tomoriteshez szukseges mappa feldolgozas.
 * Bejarja a mappat, archivalja es szerializalja az adatokat a temp fajlba.
 * Sikeres muveletek eseten 0-t ad vissza, hiba eseten negativ erteket.
 */
int prepare_directory(char *input_file, int *directory_size) {
    char current_path[PATH_MAX];
    char *sep = strrchr(input_file, '/');
    char *parent_dir = NULL;
    char *file_name = NULL;
    int archive_size = 0;
    long data_len = 0;
    int result = 0;
    
    while (true) {
        if (getcwd(current_path, sizeof(current_path)) == NULL) {
            printf("Nem sikerult elmenteni az utat.\n");
            result = DIRECTORY_ERROR;
            break;
        }
        
        /* Set the base directory for temp file creation */
        strncpy(g_temp_file_base_dir, current_path, sizeof(g_temp_file_base_dir) - 1);
        g_temp_file_base_dir[sizeof(g_temp_file_base_dir) - 1] = '\0';
        
        /* Kulso eleresi ut eseteten athelyezkedunk a szulo mappaba, hogy a tarolt utak relativak maradjanak. */
        if (sep != NULL) {
            if (sep == input_file) {
                parent_dir = strdup("/");
                if (parent_dir == NULL) {
                    result = MALLOC_ERROR;
                    break;
                }
            }
            else {
                int parent_dir_len = sep - input_file;
                parent_dir = malloc(parent_dir_len + 1);
                if (parent_dir == NULL) {
                    result = MALLOC_ERROR;
                    break;
                }
                strncpy(parent_dir, input_file, parent_dir_len);
                parent_dir[parent_dir_len] = '\0';
            }
            file_name = strdup(sep + 1);
            if (file_name == NULL) {
                result = MALLOC_ERROR;
                break;
            }
            if (chdir(parent_dir) != 0) {
                printf("Nem sikerult belepni a mappaba.\n");
                result = DIRECTORY_ERROR;
                break;
            }
        }
        
        long dir_size = archive_directory((file_name != NULL) ? file_name : input_file, &archive_size, &data_len, NULL);
        
        if (dir_size < 0) {
            if (dir_size == MALLOC_ERROR) {
                printf("Nem sikerult lefoglalni a memoriat a mappa archivallasakor.\n");
            } else if (dir_size == DIRECTORY_OPEN_ERROR) {
                printf("Nem sikerult megnyitni a mappat.\n");
            } else if (dir_size == FILE_READ_ERROR) {
                printf("Nem sikerult beolvasni egy fajlt a mappabol.\n");
            } else {
                printf("Nem sikerult a mappa archivallasa.\n");
            }
            result = dir_size;
            /* Visszalepunk az eredeti mappaba hibaeseten is. */
            if (sep != NULL) {
                if (chdir(current_path) != 0) {
                    printf("Nem sikerult kilepni a mappabol.\n");
                    result = DIRECTORY_ERROR;
                }
            }
            break;
        }
        
        *directory_size = (int)dir_size;
        
        /* Visszalepunk az eredeti mappaba. */
        if (sep != NULL) {
            if (chdir(current_path) != 0) {
                printf("Nem sikerult kilepni a mappabol.\n");
                result = DIRECTORY_ERROR;
                break;
            }
        }
        break;
    }
    
    /* Clear the global temp file base directory */
    g_temp_file_base_dir[0] = '\0';
    
    free(parent_dir);
    free(file_name);
    
    return result;
}

/*
 * Kitomoriteshez szukseges mappa feldolgozas.
 * Deszerializalja es kitomoriti az archivalt mappakat.
 * Sikeres muveletek eseten 0-t, hiba eseten negativ erteket ad vissza.
 */
int restore_directory(char *output_file, bool force, bool no_preserve_perms) {
    int res = 0;
    FILE *f = NULL;
    Directory_item item = {0};
    
    while (true) {
        f = fopen(SERIALIZED_TMP_FILE, "rb");
        if (f == NULL) {
            printf("Nem sikerult megnyitni a szerializalt fajlt.\n");
            res = FILE_READ_ERROR;
            break;
        }
        
        if (output_file != NULL) {
            if (mkdir(output_file, 0755) != 0 && errno != EEXIST) {
                printf("Nem sikerult letrehozni a kimeneti mappat.\n");
                res = MKDIR_ERROR;
                fclose(f);
                break;
            }
        }
        
        while (true) {
            item = (Directory_item){0};
            long bytes_read = deserialize_item(&item, f);
            if (bytes_read < 0) {
                if (bytes_read == MALLOC_ERROR) {
                    printf("Nem sikerult lefoglalni a memoriat a beolvasaskor.\n");
                } else {
                    printf("Nem sikerult a tomoritett mappa beolvasasa.\n");
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
            if (bytes_read == 0 || feof(f)) break;
            
            int ret = extract_directory(output_file, &item, force, no_preserve_perms);
            
            if (item.is_dir) {
                free(item.dir_path);
            } else {
                free(item.file_path);
                free(item.file_data);
            }
            
            if (ret != 0) {
                if (ret == MKDIR_ERROR) {
                    printf("Nem sikerult letrehozni egy mappat a kitomoriteskor.\n");
                } else if (ret == FILE_WRITE_ERROR) {
                    printf("Nem sikerult kiirni egy fajlt a kitomoriteskor.\n");
                } else {
                    printf("Nem sikerult a mappa kitomoritese.\n");
                }
                res = ret;
                break;
            }
        }
        
        fclose(f);
        remove(SERIALIZED_TMP_FILE);
        break;
    }
    
    return res;
}

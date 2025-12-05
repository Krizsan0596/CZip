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
long archive_directory(char *path, int *archive_size, long *data_size) {
    DIR *directory = NULL;
    long dir_size = 0;
    long result = 0;
    char *newpath = NULL;
    Directory_item current_item = {0};
    char *temp_file = malloc(strlen(path) + 6); //prefixed dot + .tmp + null terminator
    strcpy(temp_file, ".");
    strcat(temp_file, path);
    strcat(temp_file, ".tmp");
    FILE *f = fopen(temp_file, "wb");
    if (f == NULL) {
        free(temp_file);
        return FILE_READ_ERROR;
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
            *data_size += serialize_item(&root, f);
            free(root.dir_path);
        }

        directory = opendir(path);

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
                *data_size += serialize_item(&subdir, f);
                free(subdir.dir_path);
                long subdir_size = archive_directory(newpath, archive_size, data_size);
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
                data_size += serialize_item(&file, f);
                free(file.file_path);
                free(file.file_data);
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
    return result;
}

/*
 * Szerializalja az archivalt mappa elemet egy bufferbe.
 * Siker eseten a buffer meretet adja vissza, hiba eseten negativ kodot.
 */
long serialize_item(Directory_item *item, FILE *f) {
    long data_size = 0;
    long item_size = sizeof(bool) + ((item->is_dir) ? (strlen(item->dir_path) + 1 + sizeof(int)) : (sizeof(long) + strlen(item->file_path) + 1 + item->file_size));
    data_size += sizeof(long) * fwrite(&item_size, sizeof(long), 1, f);
    data_size += sizeof(bool) * fwrite(&item->is_dir, sizeof(bool), 1, f);
    if (item->is_dir) {
        data_size += sizeof(int) * fwrite(&item->perms, sizeof(int), 1, f);
        data_size += sizeof(char) * fwrite(item->dir_path, sizeof(char), strlen(item->dir_path) + 1, f);
    }
    else {
        data_size += fwrite(&item->file_size, sizeof(long), 1, f);
        data_size += fwrite(item->file_path, sizeof(char), strlen(item->file_path) + 1, f);
        data_size += fwrite(item->file_data, sizeof(char), item->file_size, f);
    }
    return data_size;
}


/*
 * Kicsomagolja az archivalt mappat a megadott utvonalra, letrehozza a mappakat es fajlokat.
 * Siker eseten 0-t ad vissza, hiba eseten negativ kodot.
 */
int extract_directory(char *path, Directory_item *archive, int archive_size, bool force, bool no_preserve_perms) {
    if (path == NULL) path = ".";
    for (int current_index = 0; current_index < archive_size; current_index++) {
        Directory_item *current = &archive[current_index];
        char *item_path = current->is_dir ? current->dir_path : current->file_path;
        char *full_path = malloc(strlen(path) + strlen(item_path) + 2);
        if (full_path == NULL) return MALLOC_ERROR;

        /* Ha a felhasznalo adott kimeneti mappat, akkor ott kezdjuk a mappa felepiteset. */
        strcpy(full_path, path);
        strcat(full_path, "/");
        strcat(full_path, item_path);
        if (current->is_dir) {
           int ret = mkdir(full_path, current->perms);
           if (ret != 0 && errno != EEXIST) {
               free(full_path);
               return MKDIR_ERROR;
           }
           if (no_preserve_perms && errno == EEXIST) {
               if (chmod(full_path, current->perms) != 0) {
                   free(full_path);
                   return MKDIR_ERROR;
               }
           }
        }
        else {
            if (current->file_size == 0) {
                /* Create an empty file */
                FILE *f = fopen(full_path, "wb");
                if (f == NULL) {
                    free(full_path);
                    return FILE_WRITE_ERROR;
                }
                fclose(f);
            } else {
                int ret = write_raw(full_path, current->file_data, current->file_size, force);
                if (ret < 0) {
                    free(full_path);
                    return FILE_WRITE_ERROR;
                }
            }
        }
        free(full_path);
    }
    return SUCCESS;
}

/*
 * Visszaalakitja a szerializalt bufferbol az archivum tombot.
 * Siker eseten az archivum meretet adja vissza, hiba eseten negativ kodot.
 */
long deserialize_item(Directory_item *item, FILE *f) {
    long archive_size;
    long read_size = 0;
    read_size += sizeof(long) * fread(&archive_size, sizeof(long), 1, f);
    item = calloc(1, sizeof(Directory_item));
    if (item == NULL) return MALLOC_ERROR;
    read_size += sizeof(bool) * fread(&item->is_dir, sizeof(bool), 1, f);
    if (item->is_dir) {
        read_size += sizeof(int) * fread(&item->perms, sizeof(int), 1, f);
        int path_len = archive_size - sizeof(bool) - sizeof(int);
        item->dir_path = malloc(sizeof(char) * path_len);
        if (item->dir_path == NULL) return MALLOC_ERROR;
        read_size += sizeof(char) * fread(item->dir_path, sizeof(char), path_len, f);
    }
    else {
        read_size += sizeof(long) * fread(&item->file_size, sizeof(long), 1, f);
        int path_len = archive_size - sizeof(bool) - sizeof(long) - item->file_size;
        item->file_path = malloc(path_len);
        if (item->file_path == NULL) return MALLOC_ERROR;
        read_size += sizeof(char) * fread(item->file_path, sizeof(char), path_len, f);
        if (item->file_size > 0) {
            item->file_data = malloc(item->file_size);
            if (item->file_data == NULL) return MALLOC_ERROR;
            read_size += sizeof(char) * fread(item->file_data, sizeof(char), item->file_size, f);
        } else {
            item->file_data = NULL;
        }
    }
    if (read_size != archive_size) return FILE_READ_ERROR;
    return archive_size;
}

/*
 * Tomoriteshez szukseges mappa feldolgozas.
 * Bejarja a mappat, archivalja es szerializalja az adatokat.
 * Sikeres muveletek eseten a szerializalt adat hosszat adja vissza, hiba eseten negativ erteket.
 */
int prepare_directory(char *input_file, char **data, int *directory_size) {
    char current_path[PATH_MAX];
    char *sep = strrchr(input_file, '/');
    char *parent_dir = NULL;
    char *file_name = NULL;
    Directory_item *archive = NULL;
    int archive_size = 0;
    int current_index = 0;
    int data_len = 0;  
    
    while (true) {
        if (getcwd(current_path, sizeof(current_path)) == NULL) {
            printf("Nem sikerult elmenteni az utat.\n");
            data_len = DIRECTORY_ERROR;
            break;
        }
        /* Kulso eleresi ut eseteten athelyezkedunk a szulo mappaba, hogy a tarolt utak relativak maradjanak. */
        if (sep != NULL) {
            if (sep == input_file) {
                parent_dir = strdup("/");
                if (parent_dir == NULL) {
                    data_len = MALLOC_ERROR;
                    break;
                }
            }
            else {
                int parent_dir_len = sep - input_file;
                parent_dir = malloc(parent_dir_len + 1);
                if (parent_dir == NULL) {
                    data_len = MALLOC_ERROR;
                    break;
                }
                strncpy(parent_dir, input_file, parent_dir_len);
                parent_dir[parent_dir_len] = '\0';
            }
            file_name = strdup(sep + 1);
            if (file_name == NULL) {
                data_len = MALLOC_ERROR;
                break;
            }
            if (chdir(parent_dir) != 0) {
                printf("Nem sikerult belepni a mappaba.\n");
                data_len = DIRECTORY_ERROR;
                break;
            }
        }
        
        *directory_size = archive_directory((file_name != NULL) ? file_name : input_file, &archive, &current_index, &archive_size);
        if (*directory_size < 0) {
            if (*directory_size == MALLOC_ERROR) {
                printf("Nem sikerult lefoglalni a memoriat a mappa archivallasakor.\n");
            } else if (*directory_size == DIRECTORY_OPEN_ERROR) {
                printf("Nem sikerult megnyitni a mappat.\n");
            } else if (*directory_size == FILE_READ_ERROR) {
                printf("Nem sikerult beolvasni egy fajlt a mappabol.\n");
            } else {
                printf("Nem sikerult a mappa archivallasa.\n");
            }
            data_len = *directory_size;
            /* Visszalepunk az eredeti mappaba hibaeseten is. */
            if (sep != NULL) {
                if (chdir(current_path) != 0) {
                    printf("Nem sikerult kilepni a mappabol.\n");
                    data_len = DIRECTORY_ERROR;
                }
            }
            break;
        }
        
        data_len = serialize_archive(archive, archive_size, data);
        if (data_len < 0) {
            if (data_len == MALLOC_ERROR) {
                printf("Nem sikerult lefoglalni a memoriat a szerializalaskor.\n");
            } else if (data_len == EMPTY_DIRECTORY) {
                printf("A mappa ures.\n");
            } else {
                printf("Nem sikerult a mappa szerializalasa.\n");
            }
            if (sep != NULL) {
                if (chdir(current_path) != 0) {
                    printf("Nem sikerult kilepni a mappabol.\n");
                    data_len = DIRECTORY_ERROR;
                    if (*data != NULL) {
                        free(*data);
                        *data = NULL;
                    }
                }
            }
            break;
        }
        
        /* Visszalepunk az eredeti mappaba. */
        if (sep != NULL) {
            if (chdir(current_path) != 0) {
                printf("Nem sikerult kilepni a mappabol.\n");
                data_len = DIRECTORY_ERROR;
                if (*data != NULL) {
                    free(*data);
                    *data = NULL;
                }
                break;
            }
        }
        break;
    }
    
    if (archive_size > 0) {
        for (int i = 0; i < archive_size; ++i) {
            if (archive[i].is_dir) {
                free(archive[i].dir_path);
            } else {
                free(archive[i].file_path);
                free(archive[i].file_data);
            }
        }
    }
    free(archive);
    free(parent_dir);
    free(file_name);
    
    return data_len;
}

/*
 * Kitomoriteshez szukseges mappa feldolgozas.
 * Deszerializalja es kitomoriti az archivalt mappakat.
 * Sikeres muveletek eseten 0-t, hiba eseten negativ erteket ad vissza.
 */
int restore_directory(char *raw_data, char *output_file, bool force, bool no_preserve_perms) {
    Directory_item *archive = NULL;
    int archive_size = 0;
    int res = 0;
    
    while (true) {
        archive_size = deserialize_archive(&archive, raw_data);
        if (archive_size < 0) {
            if (archive_size == MALLOC_ERROR) {
                printf("Nem sikerult lefoglalni a memoriat a beolvasaskor.\n");
            } else {
                printf("Nem sikerult a tomoritett mappa beolvasasa.\n");
            }
            res = archive_size;
            break;
        }
        
        if (output_file != NULL) {
            if (mkdir(output_file, 0755) != 0 && errno != EEXIST) {
                printf("Nem sikerult letrehozni a kimeneti mappat.\n");
                res = MKDIR_ERROR;
                break;
            }
        }
        
        int ret = extract_directory(output_file != NULL ? output_file : ".", archive, archive_size, force, no_preserve_perms);
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
        break;
    }
    if (archive_size > 0) {
        for (int i = 0; i < archive_size; ++i) {
            if (archive[i].is_dir) {
                free(archive[i].dir_path);
            } else {
                free(archive[i].file_path);
                free(archive[i].file_data);
            }
        }
    }
    free(archive);
    
    return res;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../lib/file.h"
#include "../lib/compress.h"
#include "../lib/decompress.h"
#include "../lib/directory.h"
#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"

/*
 * Kiirja a program hasznalati utasitasat.
 * Segitseg vagy ervenytelen kapcsolok eseten hivjuk meg.
 */
static void print_usage(const char *prog_name) {
    const char *usage =
        "Huffman kodolo\n"
        "Hasznalat: %s -c|-x [-o KIMENETI_FAJL] BEMENETI_FAJL\n"
        "\n"
        "Opciok:\n"
        "\t-c                        Tomorites\n"
        "\t-x                        Kitomorites\n"
        "\t-o KIMENETI_FAJL          Kimeneti fajl megadasa (opcionalis).\n"
        "\t-h                        Kiirja ezt az utmutatot.\n"
        "\t-f                        Ha letezik a KIMENETI_FAJL, kerdes nelkul felulirja.\n"
        "\t-r                        Rekurzivan egy megadott mappat tomorit (csak tomoriteskor szukseges).\n"
        "\t-P, --no-preserve-perms   Kitomoriteskor a tarolt jogosultsagokat alkalmazza a letrehozott mappakra is.\n"
        "\tBEMENETI_FAJL: A tomoritendo vagy visszaallitando fajl utvonala.\n"
        "\tA -c es -x kapcsolok kizarjak egymast.";

    printf(usage, prog_name);
}

/* 
 * Parancssori opciok feldolgozasa: egy mod valaszthato, az -o a kimenetet, az -f a felulirast kezeli.
 * Az elso nem kapcsolos argumentum lesz a bemeneti fajl.
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
                            printf("Az -o kapcsolo utan add meg a kimeneti fajlt.\n");
                            print_usage(argv[0]);
                            return EINVAL;
                        }
                        break;
                    default:
                        printf("Ismeretlen kapcsolo: %s\n", argv[i]);
                        print_usage(argv[0]);
                        return EINVAL;
                }
            }
        } else {
            if (args->input_file == NULL) {
                args->input_file = argv[i];
            } else {
                printf("Tobb bemeneti fajl lett megadva.\n");
                print_usage(argv[0]);
                return EINVAL;
            }
        }
    }

    /*
     * Ellenorizzuk, hogy megadtak-e a bemeneti fajlt, majd leellenorizzuk, hogy letezik-e.
     * Ha nem, kilepunk a programbol. A stat()-ot hasznaljuk, mert az fopen() nem mukodik mappakon.
     */
    if (args->input_file == NULL) {
        printf("Nem lett bemeneti fajl megadva.\n");
        print_usage(argv[0]);
        return EINVAL;
    }
    
    struct stat st;
    if (stat(args->input_file, &st) != 0) {
        printf("A (%s) fajl nem talalhato.\n", args->input_file);
        print_usage(argv[0]);
        return FILE_READ_ERROR;
    }

    if (args->compress_mode && args->extract_mode) {
        printf("A -c es -x kapcsolok kizarjak egymast.\n");
        print_usage(argv[0]);
        return EINVAL;
    }

    return SUCCESS;
}

/*
 * A kapcsolokat feldolgozva elinditja a tomorites vagy kitomorites folyamatat, es a hibakodot adja vissza.
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

    /* Ellenorizzuk, hogy az -r valoban mappat jelol, vagy hibasan lett megadva. */
    if (args.directory) {
        struct stat st;
        int ret = stat(args.input_file, &st);
        if (ret != 0) {
            printf("Nem sikerult ellenorizni a mappat.\n");
            return ret;
        }
        else if (S_ISREG(st.st_mode)) args.directory = false;
    }
    else {
        struct stat st;
        int ret = stat(args.input_file, &st);
        if (ret != 0) {
            printf("Nem sikerult ellenorizni a fajlt.\n");
            return ret;
        }
        else if (S_ISDIR(st.st_mode)) {
            printf("Az -r kapcsolo nelkul nem tomorit mappat a program.\n");
            print_usage(argv[0]);
            return EISDIR;
        }
    }
    
    if (args.compress_mode) {
        char *data = NULL;
        long data_len = 0;
        long directory_size = 0;
        int directory_size_int = 0;

        if (args.directory) {
            int prep_res = prepare_directory(args.input_file, &directory_size_int);
            if (prep_res < 0) {
                return prep_res;
            }
            directory_size = directory_size_int;
            int read_res = read_raw(SERIALIZED_TMP_FILE, &data);
            if (read_res < 0) {
                printf("Nem sikerult beolvasni a szerializalt adatokat.\n");
                return read_res;
            }
            data_len = read_res;
        } else {
            int read_res = read_raw(args.input_file, &data);
            if (read_res < 0) {
                if (read_res == EMPTY_FILE) {
                    printf("A fajl (%s) ures.\n", args.input_file);
                } else {
                    printf("Nem sikerult megnyitni a fajlt (%s).\n", args.input_file);
                }
                return read_res;
            }
            data_len = read_res;
            directory_size = data_len;
        }

        int compress_res = run_compression(args, data, data_len, directory_size);
        free(data);
        return compress_res;
    } else if (args.extract_mode) {
        char *raw_data = NULL;
        long raw_size = 0;
        bool is_dir = false;
        char *original_name = NULL;

        int decomp_res = run_decompression(args, &raw_data, &raw_size, &is_dir, &original_name);
        if (decomp_res != 0) {
            free(raw_data);
            free(original_name);
            return decomp_res;
        }

        int res = 0;
        if (is_dir) {
            FILE *f = fopen(SERIALIZED_TMP_FILE, "wb");
            if (f == NULL || fwrite(raw_data, 1, raw_size, f) != (size_t)raw_size) {
                printf("Nem sikerult kiirni a szerializalt adatokat.\n");
                if (f != NULL) fclose(f);
                free(raw_data);
                free(original_name);
                return FILE_WRITE_ERROR;
            }
            fclose(f);
            res = restore_directory(args.output_file, args.force, args.no_preserve_perms);
        } else {
            char *target = args.output_file != NULL ? args.output_file : original_name;
            int write_res = write_raw(target, raw_data, raw_size, args.force);
            if (write_res < 0) {
                printf("Hiba tortent a kimeneti fajl (%s) irasa kozben.\n", target);
                res = EIO;
            }
        }

        free(raw_data);
        free(original_name);
        return res;
    }
    else {
        printf("Az egyik modot (-c vagy -x) meg kell adni.\n");
        print_usage(argv[0]);
        return EINVAL;
    }
}

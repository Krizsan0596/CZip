#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>
#include "../lib/directory.h"
#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"

// Helper function to recursively delete a directory
static int remove_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return unlink(path);
    }

    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                int ret = remove_directory_recursive(full_path);
                if (ret != 0) result = ret;
            } else {
                int ret = unlink(full_path);
                if (ret != 0) result = ret;
            }
        } else {
            // Attempt to remove anyway (could be a broken symlink, etc.)
            int ret = unlink(full_path);
            if (ret != 0) result = ret;
        }
    }
    closedir(dir);
    int ret = rmdir(path);
    return (result != 0) ? result : ret;
}

// Function to recursively compare two directories
int compare_directories(const char *path1, const char *path2) {
    DIR *dir1 = opendir(path1);
    if (dir1 == NULL) {
        perror("opendir path1");
        return -1;
    }

    DIR *dir2 = opendir(path2);
    if (dir2 == NULL) {
        perror("opendir path2");
        closedir(dir1);
        return -1;
    }

    struct dirent *entry1;
    while ((entry1 = readdir(dir1)) != NULL) {
        if (strcmp(entry1->d_name, ".") == 0 || strcmp(entry1->d_name, "..") == 0) {
            continue;
        }

        char full_path1[1024];
        snprintf(full_path1, sizeof(full_path1), "%s/%s", path1, entry1->d_name);

        char full_path2[1024];
        snprintf(full_path2, sizeof(full_path2), "%s/%s", path2, entry1->d_name);

        struct stat stat1;
        if (stat(full_path1, &stat1) != 0) {
            perror("stat path1");
            closedir(dir1);
            closedir(dir2);
            return -1;
        }

        struct stat stat2;
        if (stat(full_path2, &stat2) != 0) {
            perror("stat path2");
            closedir(dir1);
            closedir(dir2);
            return -1;
        }

        if (S_ISDIR(stat1.st_mode) != S_ISDIR(stat2.st_mode)) {
            fprintf(stderr, "Type mismatch for %s\n", entry1->d_name);
            closedir(dir1);
            closedir(dir2);
            return -1;
        }

        if (S_ISDIR(stat1.st_mode)) {
            if (compare_directories(full_path1, full_path2) != 0) {
                closedir(dir1);
                closedir(dir2);
                return -1;
            }
        } else {
            if (stat1.st_size != stat2.st_size) {
                fprintf(stderr, "Size mismatch for %s\n", entry1->d_name);
                closedir(dir1);
                closedir(dir2);
                return -1;
            }

            FILE *f1 = fopen(full_path1, "rb");
            if (f1 == NULL) {
                perror("fopen path1");
                closedir(dir1);
                closedir(dir2);
                return -1;
            }

            FILE *f2 = fopen(full_path2, "rb");
            if (f2 == NULL) {
                perror("fopen path2");
                fclose(f1);
                closedir(dir1);
                closedir(dir2);
                return -1;
            }

            char buf1[1024];
            char buf2[1024];
            size_t bytes_read1;
            while ((bytes_read1 = fread(buf1, 1, sizeof(buf1), f1)) > 0) {
                size_t bytes_read2 = fread(buf2, 1, sizeof(buf2), f2);
                if (bytes_read1 != bytes_read2 || memcmp(buf1, buf2, bytes_read1) != 0) {
                    fprintf(stderr, "Content mismatch for %s\n", entry1->d_name);
                    fclose(f1);
                    fclose(f2);
                    closedir(dir1);
                    closedir(dir2);
                    return -1;
                }
            }

            fclose(f1);
            fclose(f2);
        }
    }

    closedir(dir1);
    closedir(dir2);
    return 0;
}


int main() {
    // ==========================================
    // Test prepare_directory function
    // ==========================================
    printf("Testing prepare_directory and restore_directory functions...\n");

    // Create test directory for tests
    char *test_dir = "../tests/prep_test_dir";
    char *output_dir = "prep_output_dir";
    
    mkdir("../tests", 0755);
    mkdir("../tests/prep_test_dir", 0755);
    mkdir("../tests/prep_test_dir/subdir", 0755);
    
    FILE *pf1 = fopen("../tests/prep_test_dir/file1.txt", "w");
    if (pf1) {
        fprintf(pf1, "This is file1 for prepare_directory test.\n");
        fclose(pf1);
    }
    FILE *pf2 = fopen("../tests/prep_test_dir/subdir/file2.txt", "w");
    if (pf2) {
        fprintf(pf2, "This is file2 for prepare_directory test.\n");
        fclose(pf2);
    }

    // Test 1: prepare_directory with relative path
    printf("  Test 1: prepare_directory with relative path...\n");
    {
        int directory_size = 0;
        int result = prepare_directory(test_dir, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed with relative path, code: %d\n", result);
            return 1;
        }
        if (directory_size <= 0) {
            fprintf(stderr, "Error: prepare_directory returned invalid directory_size: %d\n", directory_size);
            return 1;
        }
        printf("    Relative path test passed. Directory size: %d bytes\n", directory_size);
        
        // Verify temp file was created
        struct stat st;
        if (stat(SERIALIZED_TMP_FILE, &st) != 0) {
            fprintf(stderr, "Error: Temp file was not created\n");
            return 1;
        }
        
        // Cleanup temp file
        remove(SERIALIZED_TMP_FILE);
    }
    
    // Test 2: prepare_directory with absolute path (includes full round-trip verification)
    printf("  Test 2: prepare_directory with absolute path and restore...\n");
    {
        // Get absolute path to test directory
        char abs_path[1024];
        char saved_cwd[1024];
        if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(test_dir) != 0) {
            perror("chdir error");
            return 1;
        }
        if (getcwd(abs_path, sizeof(abs_path)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(saved_cwd) != 0) {
            perror("chdir error");
            return 1;
        }
        
        int directory_size = 0;
        int result = prepare_directory(abs_path, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed with absolute path, code: %d\n", result);
            return 1;
        }
        if (directory_size <= 0) {
            fprintf(stderr, "Error: prepare_directory returned invalid directory_size for absolute path: %d\n", directory_size);
            return 1;
        }
        printf("    Absolute path test passed. Directory size: %d bytes\n", directory_size);
        
        // Now test restore_directory
        remove_directory_recursive(output_dir);
        mkdir(output_dir, 0755);
        
        result = restore_directory(output_dir, true, false);
        if (result != 0) {
            fprintf(stderr, "Error: restore_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Build path to compare - the extracted dir should be output_dir/<dirname>
        // Extract directory name from test_dir (last component after last '/')
        char *dirname = strrchr(test_dir, '/');
        dirname = (dirname != NULL) ? dirname + 1 : test_dir;
        char extracted_path[1024];
        snprintf(extracted_path, sizeof(extracted_path), "%s/%s", output_dir, dirname);
        
        // Compare directories
        if (compare_directories(test_dir, extracted_path) != 0) {
            fprintf(stderr, "Error: Directories do not match after prepare_directory with absolute path\n");
            return 1;
        }
        printf("    Absolute path round-trip verification passed.\n");
        
        // Cleanup
        remove_directory_recursive(output_dir);
    }
    
    // Test 3: prepare_directory with non-existent path (error handling)
    printf("  Test 3: prepare_directory with non-existent path...\n");
    {
        int directory_size = 0;
        int result = prepare_directory("./non_existent_directory_12345", &directory_size);
        if (result >= 0) {
            fprintf(stderr, "Error: prepare_directory should fail for non-existent directory\n");
            return 1;
        }
        printf("    Non-existent path error handling test passed. Error code: %d\n", result);
    }
    
    // Test 4: Verify working directory is restored after prepare_directory
    printf("  Test 4: Verify working directory is preserved...\n");
    {
        char cwd_before[1024];
        char cwd_after[1024];
        
        if (getcwd(cwd_before, sizeof(cwd_before)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        
        int directory_size = 0;
        int result = prepare_directory(test_dir, &directory_size);
        
        if (getcwd(cwd_after, sizeof(cwd_after)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed, code: %d\n", result);
            return 1;
        }
        
        if (strcmp(cwd_before, cwd_after) != 0) {
            fprintf(stderr, "Error: Working directory changed after prepare_directory!\n");
            fprintf(stderr, "  Before: %s\n", cwd_before);
            fprintf(stderr, "  After:  %s\n", cwd_after);
            return 1;
        }
        printf("    Working directory preservation test passed.\n");
        remove(SERIALIZED_TMP_FILE);
    }
    
    // Test 5: restore_directory with NULL output path (restore to current directory)
    printf("  Test 5: restore_directory with NULL output path...\n");
    {
        // Get absolute path to test directory
        char abs_path[1024];
        char saved_cwd[1024];
        if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(test_dir) != 0) {
            perror("chdir error");
            return 1;
        }
        if (getcwd(abs_path, sizeof(abs_path)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(saved_cwd) != 0) {
            perror("chdir error");
            return 1;
        }
        
        int directory_size = 0;
        int result = prepare_directory(abs_path, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Extract directory name
        char *dir_name = strrchr(abs_path, '/');
        dir_name = (dir_name != NULL) ? dir_name + 1 : abs_path;
        
        // Clean any existing directory with same name in current directory
        remove_directory_recursive(dir_name);
        
        // Use restore_directory with NULL output
        result = restore_directory(NULL, true, false);
        if (result != 0) {
            fprintf(stderr, "Error: restore_directory with NULL output failed, code: %d\n", result);
            return 1;
        }
        
        // Verify extraction
        if (compare_directories(test_dir, dir_name) != 0) {
            fprintf(stderr, "Error: Directories do not match after restore_directory with NULL output\n");
            remove_directory_recursive(dir_name);
            return 1;
        }
        
        printf("    restore_directory with NULL output test passed.\n");
        
        // Cleanup
        remove_directory_recursive(dir_name);
    }
    
    // Test 6: restore_directory with force flag (overwrite existing)
    printf("  Test 6: restore_directory with force flag...\n");
    {
        // Get absolute path
        char abs_path[1024];
        char saved_cwd[1024];
        if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(test_dir) != 0) {
            perror("chdir error");
            return 1;
        }
        if (getcwd(abs_path, sizeof(abs_path)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(saved_cwd) != 0) {
            perror("chdir error");
            return 1;
        }
        
        int directory_size = 0;
        int result = prepare_directory(abs_path, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Clean output directory and create fresh
        remove_directory_recursive(output_dir);
        
        // First extraction
        result = restore_directory(output_dir, true, false);
        if (result != 0) {
            fprintf(stderr, "Error: first restore_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Prepare again for second extraction
        result = prepare_directory(abs_path, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed on second call, code: %d\n", result);
            return 1;
        }
        
        // Second extraction with force flag (should overwrite)
        result = restore_directory(output_dir, true, false);
        if (result != 0) {
            fprintf(stderr, "Error: second restore_directory with force failed, code: %d\n", result);
            return 1;
        }
        
        printf("    restore_directory with force flag test passed.\n");
        
        // Cleanup
        remove_directory_recursive(output_dir);
    }
    
    // Cleanup test directory
    remove_directory_recursive(test_dir);
    
    printf("All prepare_directory and restore_directory tests passed!\n");

    // ==========================================
    // Test directory permissions preservation
    // ==========================================
    printf("Testing directory permissions preservation...\n");
    
    // Create test directories with specific non-default permissions
    char *perm_test_dir = "../tests/perm_test_dir";
    char *perm_output_dir = "perm_output_dir";
    
    // Create test directory structure with non-default permissions
    mkdir("../tests", 0755);
    mkdir("../tests/perm_test_dir", 0755);
    mkdir("../tests/perm_test_dir/subdir_700", 0700);  // Owner only (rwx------)
    mkdir("../tests/perm_test_dir/subdir_750", 0750);  // Owner + group read (rwxr-x---)
    mkdir("../tests/perm_test_dir/subdir_755", 0755);  // Standard permissions (rwxr-xr-x)
    
    // Create test files in the directories
    FILE *perm_f1 = fopen("../tests/perm_test_dir/file1.txt", "w");
    if (perm_f1) {
        fprintf(perm_f1, "File in root directory.\n");
        fclose(perm_f1);
    }
    FILE *perm_f2 = fopen("../tests/perm_test_dir/subdir_700/file2.txt", "w");
    if (perm_f2) {
        fprintf(perm_f2, "File in subdir_700.\n");
        fclose(perm_f2);
    }
    FILE *perm_f3 = fopen("../tests/perm_test_dir/subdir_750/file3.txt", "w");
    if (perm_f3) {
        fprintf(perm_f3, "File in subdir_750.\n");
        fclose(perm_f3);
    }
    FILE *perm_f4 = fopen("../tests/perm_test_dir/subdir_755/file4.txt", "w");
    if (perm_f4) {
        fprintf(perm_f4, "File in subdir_755.\n");
        fclose(perm_f4);
    }
    
    // Get original permissions using stat()
    struct stat perm_st_700, perm_st_750, perm_st_755;
    int perm_700_mode = 0, perm_750_mode = 0, perm_755_mode = 0;
    
    if (stat("../tests/perm_test_dir/subdir_700", &perm_st_700) == 0) {
        perm_700_mode = perm_st_700.st_mode & 0777;
    } else {
        fprintf(stderr, "Error: Could not stat subdir_700\n");
        return 1;
    }
    if (stat("../tests/perm_test_dir/subdir_750", &perm_st_750) == 0) {
        perm_750_mode = perm_st_750.st_mode & 0777;
    } else {
        fprintf(stderr, "Error: Could not stat subdir_750\n");
        return 1;
    }
    if (stat("../tests/perm_test_dir/subdir_755", &perm_st_755) == 0) {
        perm_755_mode = perm_st_755.st_mode & 0777;
    } else {
        fprintf(stderr, "Error: Could not stat subdir_755\n");
        return 1;
    }
    
    printf("  Original permissions: subdir_700=%04o, subdir_750=%04o, subdir_755=%04o\n", 
           perm_700_mode, perm_750_mode, perm_755_mode);
    
    // Test: Full round-trip preserving directory permissions
    printf("  Test: Full round-trip preserving directory permissions...\n");
    {
        // Get absolute path
        char abs_path[1024];
        char saved_cwd[1024];
        if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(perm_test_dir) != 0) {
            perror("chdir error");
            return 1;
        }
        if (getcwd(abs_path, sizeof(abs_path)) == NULL) {
            perror("getcwd error");
            return 1;
        }
        if (chdir(saved_cwd) != 0) {
            perror("chdir error");
            return 1;
        }
        
        // Prepare directory
        int directory_size = 0;
        int result = prepare_directory(abs_path, &directory_size);
        if (result < 0) {
            fprintf(stderr, "Error: prepare_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Extract the directory
        remove_directory_recursive(perm_output_dir);
        mkdir(perm_output_dir, 0755);
        
        result = restore_directory(perm_output_dir, true, false);
        if (result != 0) {
            fprintf(stderr, "Error: restore_directory failed, code: %d\n", result);
            return 1;
        }
        
        // Extract directory name
        char *dir_name = strrchr(abs_path, '/');
        dir_name = (dir_name != NULL) ? dir_name + 1 : abs_path;
        
        // Verify extracted directory permissions using stat()
        struct stat ext_st_700, ext_st_750, ext_st_755;
        int ext_700_mode = 0, ext_750_mode = 0, ext_755_mode = 0;
        
        char ext_700_path[256], ext_750_path[256], ext_755_path[256];
        snprintf(ext_700_path, sizeof(ext_700_path), "%s/%s/subdir_700", perm_output_dir, dir_name);
        snprintf(ext_750_path, sizeof(ext_750_path), "%s/%s/subdir_750", perm_output_dir, dir_name);
        snprintf(ext_755_path, sizeof(ext_755_path), "%s/%s/subdir_755", perm_output_dir, dir_name);
        
        if (stat(ext_700_path, &ext_st_700) == 0) {
            ext_700_mode = ext_st_700.st_mode & 0777;
        } else {
            fprintf(stderr, "Error: Could not stat extracted subdir_700\n");
            return 1;
        }
        if (stat(ext_750_path, &ext_st_750) == 0) {
            ext_750_mode = ext_st_750.st_mode & 0777;
        } else {
            fprintf(stderr, "Error: Could not stat extracted subdir_750\n");
            return 1;
        }
        if (stat(ext_755_path, &ext_st_755) == 0) {
            ext_755_mode = ext_st_755.st_mode & 0777;
        } else {
            fprintf(stderr, "Error: Could not stat extracted subdir_755\n");
            return 1;
        }
        
        printf("  Extracted permissions: subdir_700=%04o, subdir_750=%04o, subdir_755=%04o\n", 
               ext_700_mode, ext_750_mode, ext_755_mode);
        
        // Compare permissions
        if (perm_700_mode != ext_700_mode) {
            fprintf(stderr, "Error: subdir_700 permissions mismatch! Original: %04o, Extracted: %04o\n",
                    perm_700_mode, ext_700_mode);
            return 1;
        }
        if (perm_750_mode != ext_750_mode) {
            fprintf(stderr, "Error: subdir_750 permissions mismatch! Original: %04o, Extracted: %04o\n",
                    perm_750_mode, ext_750_mode);
            return 1;
        }
        if (perm_755_mode != ext_755_mode) {
            fprintf(stderr, "Error: subdir_755 permissions mismatch! Original: %04o, Extracted: %04o\n",
                    perm_755_mode, ext_755_mode);
            return 1;
        }
        
        printf("    All directory permissions preserved correctly!\n");
        
        // Cleanup
        remove_directory_recursive(perm_output_dir);
    }
    
    // Cleanup test directory
    remove_directory_recursive(perm_test_dir);
    
    printf("All directory permissions tests passed!\n");

    return 0;
}

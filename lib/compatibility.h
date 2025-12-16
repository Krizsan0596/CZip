#ifndef COMPATIBILITY_H
#define COMPATIBILITY_H


#ifdef _WIN32
    #define PROGRAM_USAGE_TEXT \
        "Huffman encoder\n" \
        "Usage: %s -c|-x [-o OUTPUT_FILE] INPUT_FILE\n" \
        "\n" \
        "Options:\n" \
        "\t-c                        Compress\n" \
        "\t-x                        Decompress\n" \
        "\t-o OUTPUT_FILE            Set output file (optional).\n" \
        "\t-h                        Show this guide.\n" \
        "\t-f                        Overwrite OUTPUT_FILE without asking if it exists.\n" \
        "\t-r                        Recursively compress a directory (only needed for compression).\n" \
        "\tINPUT_FILE: Path to the file to compress or restore.\n" \
        "\tThe -c and -x options are mutually exclusive."

    /* Windows headers */
    #include <io.h>
    #include <direct.h>
    #include <windows.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <mman.h>
    #include <stdarg.h>
    #include <wchar.h>
    #include <stdio.h>
    #include <errno.h>
    
    /* UTF-8 Wrappers for Windows */
    static inline int win32_open_utf8(const char *path, int oflag, ...) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
            errno = ENOENT;
            return -1;
        }
        int pmode = 0;
        if (oflag & _O_CREAT) {
            va_list ap;
            va_start(ap, oflag);
            pmode = va_arg(ap, int);
            va_end(ap);
        }
        return _wopen(wpath, oflag, pmode);
    }

    static inline FILE *win32_fopen_utf8(const char *path, const char *mode) {
        wchar_t wpath[MAX_PATH];
        wchar_t wmode[10];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) return NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 10) == 0) return NULL;
        return _wfopen(wpath, wmode);
    }

    static inline int win32_mkdir_utf8(const char *path) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _wmkdir(wpath);
    }

    static inline int win32_stat_utf8(const char *path, struct _stat *buffer) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _wstat(wpath, buffer);
    }
    
    static inline int win32_access_utf8(const char *path, int mode) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _waccess(wpath, mode);
    }
    
    static inline int win32_unlink_utf8(const char *path) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _wunlink(wpath);
    }
    
    static inline int win32_rmdir_utf8(const char *path) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _wrmdir(wpath);
    }

    static inline int win32_chdir_utf8(const char *path) {
        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) {
             errno = ENOENT;
             return -1;
        }
        return _wchdir(wpath);
    }

    static inline char *win32_getcwd_utf8(char *buffer, int maxlen) {
        wchar_t wpath[MAX_PATH];
        if (_wgetcwd(wpath, MAX_PATH) == NULL) return NULL;
        if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buffer, maxlen, NULL, NULL) == 0) return NULL;
        return buffer;
    }

    /* Map POSIX functions to Windows equivalents */
    #define open win32_open_utf8
    #define fopen win32_fopen_utf8
    #define close _close
    #define read _read
    #define write _write
    #define lseek _lseek
    #define access win32_access_utf8
    #define unlink win32_unlink_utf8
    #define rmdir win32_rmdir_utf8
    #define mkdir(path, mode) win32_mkdir_utf8(path)
    #define chdir win32_chdir_utf8
    #define getcwd win32_getcwd_utf8
    #define stat(path, buf) win32_stat_utf8(path, (struct _stat*)(buf))
    #define fstat _fstat
    #define chmod _chmod
    #define ftruncate(fd, size) _chsize_s(fd, size)
    #define usleep(us) Sleep((us) / 1000)
    
    /* POSIX file access modes */
    #define F_OK 0
    #define R_OK 4
    #define W_OK 2
    #define X_OK 1
    
    /* POSIX open flags */
    #define O_RDONLY _O_RDONLY
    #define O_WRONLY _O_WRONLY
    #define O_RDWR _O_RDWR
    #define O_CREAT _O_CREAT
    #define O_TRUNC _O_TRUNC
    #define O_APPEND _O_APPEND
    #define O_BINARY _O_BINARY
    
    #define FILE_MODE (_S_IREAD | _S_IWRITE)
    #define DIR_MODE  0

    /* File permission macros */
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
    static inline void convert_path(char *path) {
        while (*path != '\0') {
            if (*path == '\\') *path = '/';
            path++;
        }
    }
    
#else
    #define PROGRAM_USAGE_TEXT \
        "Huffman encoder\n" \
        "Usage: %s -c|-x [-o OUTPUT_FILE] INPUT_FILE\n" \
        "\n" \
        "Options:\n" \
        "\t-c                        Compress\n" \
        "\t-x                        Decompress\n" \
        "\t-o OUTPUT_FILE            Set output file (optional).\n" \
        "\t-h                        Show this guide.\n" \
        "\t-f                        Overwrite OUTPUT_FILE without asking if it exists.\n" \
        "\t-r                        Recursively compress a directory (only needed for compression).\n" \
        "\t-P, --no-preserve-perms   When extracting, apply stored permissions even to existing directories.\n" \
        "\tINPUT_FILE: Path to the file to compress or restore.\n" \
        "\tThe -c and -x options are mutually exclusive."

    /* POSIX/Unix/Linux headers */
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/mman.h>
    static inline void convert_path(char *path) {
        (void)path;
    }
    #define FILE_MODE 0666
    #define DIR_MODE  0777
#endif

#endif /* COMPATIBILITY_H */

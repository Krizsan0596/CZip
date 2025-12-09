#ifndef COMPATIBILITY_H
#define COMPATIBILITY_H

#ifdef _WIN32
    /* Windows headers */
    #include <io.h>
    #include <direct.h>
    #include <windows.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <mman.h>
    
    /* Map POSIX functions to Windows equivalents */
    #define open _open
    #define close _close
    #define read _read
    #define write _write
    #define lseek _lseek
    #define access _access
    #define unlink _unlink
    #define rmdir _rmdir
    #define mkdir(path, mode) _mkdir(path)
    #define chdir _chdir
    #define getcwd _getcwd
    #define stat _stat
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
    
    /* File permission macros */
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
    
#else
    /* POSIX/Unix/Linux headers */
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/mman.h>
#endif

#endif /* COMPATIBILITY_H */

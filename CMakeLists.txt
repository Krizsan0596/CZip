cmake_minimum_required(VERSION 3.10)
project(huffman)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(${PROJECT_NAME}
    src/main.c
    lib/file.c
    lib/compress.c
    lib/decompress.c
    lib/directory.c
)

target_include_directories(${PROJECT_NAME} PRIVATE lib)

add_executable(file_io_test tests/test_file_io.c lib/file.c lib/compress.c lib/directory.c)
target_include_directories(file_io_test PRIVATE lib)
add_test(NAME FileIOTest COMMAND file_io_test)

add_executable(compress_test tests/test_compress.c lib/compress.c lib/file.c lib/directory.c)
target_include_directories(compress_test PRIVATE lib)
add_test(NAME CompressTest COMMAND compress_test)

add_executable(test_compress_decompress tests/test_compress_decompress.c lib/compress.c lib/decompress.c lib/file.c lib/directory.c)
target_include_directories(test_compress_decompress PRIVATE lib)
add_test(NAME CompressDecompressTest COMMAND test_compress_decompress)

add_executable(directory_test tests/test_directory.c lib/directory.c lib/file.c lib/compress.c)
target_include_directories(directory_test PRIVATE lib)
add_test(NAME DirectoryTest COMMAND directory_test)

IF (WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE /W0)
    include(FetchContent)
    FetchContent_Declare(
        dirent
        URL https://github.com/tronkko/dirent/archive/refs/tags/1.26.zip
    )
    FetchContent_MakeAvailable(dirent)

    FetchContent_Declare(
        mman
        URL https://github.com/alitrack/mman-win32/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(mman)

    # Expose headers for the fetched libraries so MSVC can find dirent.h and mman.h at compile time.
    set(WIN_EXTRA_INCLUDES
        ${dirent_SOURCE_DIR}/include
        ${mman_SOURCE_DIR}/include
    )

    target_include_directories(${PROJECT_NAME} PRIVATE ${WIN_EXTRA_INCLUDES})
    target_link_libraries(${PROJECT_NAME} PRIVATE dirent mman)

    target_include_directories(file_io_test PRIVATE ${WIN_EXTRA_INCLUDES})
    target_link_libraries(file_io_test mman)

    target_include_directories(compress_test PRIVATE ${WIN_EXTRA_INCLUDES})
    target_link_libraries(compress_test dirent mman)

    target_include_directories(test_compress_decompress PRIVATE ${WIN_EXTRA_INCLUDES})
    target_link_libraries(test_compress_decompress mman)

    target_include_directories(directory_test PRIVATE ${WIN_EXTRA_INCLUDES})
    target_link_libraries(directory_test dirent mman)
ELSE()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -g)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
    target_link_libraries(file_io_test m)
    target_link_libraries(compress_test m)
    target_link_libraries(test_compress_decompress m)
    target_link_libraries(directory_test m)
ENDIF()

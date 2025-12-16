cmake_minimum_required(VERSION 3.10)
project(huffman)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

# Create static library with core modules
add_library(czip_core STATIC
    lib/file.c
    lib/compress.c
    lib/decompress.c
    lib/directory.c
)

target_include_directories(czip_core PUBLIC lib)

# Main executable
add_executable(${PROJECT_NAME}
    src/main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE czip_core)

# Test executables
add_executable(file_io_test tests/test_file_io.c)
target_link_libraries(file_io_test PRIVATE czip_core)
add_test(NAME FileIOTest COMMAND file_io_test)

add_executable(compress_test tests/test_compress.c)
target_link_libraries(compress_test PRIVATE czip_core)
add_test(NAME CompressTest COMMAND compress_test)

add_executable(test_compress_decompress tests/test_compress_decompress.c)
target_link_libraries(test_compress_decompress PRIVATE czip_core)
add_test(NAME CompressDecompressTest COMMAND test_compress_decompress)

add_executable(directory_test tests/test_directory.c)
target_link_libraries(directory_test PRIVATE czip_core)
add_test(NAME DirectoryTest COMMAND directory_test)

IF (WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE /W0)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    include(FetchContent)
    
    # Fetch dirent and compile as static library
    FetchContent_Declare(
        dirent
        URL https://github.com/tronkko/dirent/archive/refs/tags/1.26.zip
    )
    FetchContent_MakeAvailable(dirent)
    
    # Create static library for dirent (header-only, create empty lib)
    add_library(dirent_static INTERFACE)
    target_include_directories(dirent_static INTERFACE ${dirent_SOURCE_DIR}/include)

    # Fetch mman and compile as static library
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    FetchContent_Declare(
        mman
        URL https://github.com/alitrack/mman-win32/archive/refs/heads/master.zip
    )
    FetchContent_Populate(mman)
    
    # Create static library for mman
    add_library(mman_static STATIC ${mman_SOURCE_DIR}/mman.c)
    target_include_directories(mman_static PUBLIC ${mman_SOURCE_DIR})

    # Expose headers for the fetched libraries
    set(WIN_EXTRA_INCLUDES
        ${dirent_SOURCE_DIR}/include
        ${mman_SOURCE_DIR}
    )

    target_include_directories(czip_core PUBLIC ${WIN_EXTRA_INCLUDES})
    target_link_libraries(czip_core PUBLIC dirent_static mman_static)
ELSE()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -g)
    target_link_libraries(czip_core PUBLIC m)
ENDIF()

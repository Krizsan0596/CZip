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

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror -g)
target_include_directories(${PROJECT_NAME} PRIVATE lib)
target_link_libraries(${PROJECT_NAME} PRIVATE m)

add_executable(file_io_test tests/test_file_io.c lib/file.c lib/compress.c lib/directory.c)
target_include_directories(file_io_test PRIVATE lib)
target_link_libraries(file_io_test m)
add_test(NAME FileIOTest COMMAND file_io_test)

add_executable(compress_test tests/test_compress.c lib/compress.c lib/file.c lib/directory.c)
target_include_directories(compress_test PRIVATE lib)
target_link_libraries(compress_test m)
add_test(NAME CompressTest COMMAND compress_test)

add_executable(test_compress_decompress tests/test_compress_decompress.c lib/compress.c lib/decompress.c lib/file.c lib/directory.c)
target_include_directories(test_compress_decompress PRIVATE lib)
target_link_libraries(test_compress_decompress m)
add_test(NAME CompressDecompressTest COMMAND test_compress_decompress)

add_executable(directory_test tests/test_directory.c lib/directory.c lib/file.c lib/compress.c)
target_include_directories(directory_test PRIVATE lib)
target_link_libraries(directory_test m)
add_test(NAME DirectoryTest COMMAND directory_test)

add_executable(cli_test tests/test_cli.c lib/file.c lib/compress.c lib/decompress.c lib/directory.c)
target_include_directories(cli_test PRIVATE lib)
target_link_libraries(cli_test m)
add_test(NAME CLITest COMMAND cli_test)

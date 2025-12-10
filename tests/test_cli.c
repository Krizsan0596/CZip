#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../lib/data_types.h"
#include "../lib/debugmalloc.h"

// Rename the production main to avoid symbol conflicts in this test binary.
#define main huffman_main
#include "../src/main.c"
#undef main

static char *create_temp_file(const char *content) {
    char template[] = "/tmp/czip_cli_fileXXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);

    if (content != NULL) {
        size_t len = strlen(content);
        assert(write(fd, content, len) == (ssize_t)len);
    }

    close(fd);
    return strdup(template);
}

static char *create_temp_dir(void) {
    char template[] = "/tmp/czip_cli_dirXXXXXX";
    char *dir_path = mkdtemp(template);
    assert(dir_path != NULL);
    return strdup(dir_path);
}

static char *capture_stream_output(FILE *stream, int (*invoker)(void *), void *context, int *return_code) {
    char template[] = "/tmp/czip_cli_stderrXXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    FILE *tmp = fdopen(fd, "w+");
    assert(tmp != NULL);

    int original_fd = dup(fileno(stream));
    assert(original_fd >= 0);
    assert(dup2(fd, fileno(stream)) >= 0);

    *return_code = invoker(context);

    fflush(stream);
    assert(dup2(original_fd, fileno(stream)) >= 0);
    close(original_fd);

    fseek(tmp, 0, SEEK_END);
    long length = ftell(tmp);
    assert(length >= 0);
    fseek(tmp, 0, SEEK_SET);

    char *buffer = calloc((size_t)length + 1, 1);
    assert(buffer != NULL);
    fread(buffer, 1, (size_t)length, tmp);

    fclose(tmp);
    unlink(template);

    return buffer;
}

static int invoke_parse(void *context) {
    struct {
        int argc;
        char **argv;
        Arguments *args;
    } *params = context;
    return parse_arguments(params->argc, params->argv, params->args);
}

static int invoke_main(void *context) {
    struct {
        int argc;
        char **argv;
    } *params = context;
    return huffman_main(params->argc, params->argv);
}

static void test_no_input_provided(void) {
    Arguments args;
    int argc = 1;
    char *argv[] = {"czip"};

    int ret = 0;
    char *stderr_output = capture_stream_output(stderr, invoke_parse,
        &(struct {int argc; char **argv; Arguments *args;}){argc, argv, &args}, &ret);

    assert(ret == EINVAL);
    assert(strstr(stderr_output, "No input file was provided.") != NULL);

    free(stderr_output);
}

static void test_missing_output_value(void) {
    Arguments args;
    int argc = 2;
    char *argv[] = {"czip", "-o"};

    int ret = 0;
    char *stderr_output = capture_stream_output(stderr, invoke_parse,
        &(struct {int argc; char **argv; Arguments *args;}){argc, argv, &args}, &ret);

    assert(ret == EINVAL);
    assert(strstr(stderr_output, "Provide the output file after the -o option.") != NULL);

    free(stderr_output);
}

static void test_conflicting_modes(void) {
    char *input_path = create_temp_file("hello");
    Arguments args;
    int argc = 4;
    char *argv[] = {"czip", "-c", "-x", input_path};

    int ret = 0;
    char *stderr_output = capture_stream_output(stderr, invoke_parse,
        &(struct {int argc; char **argv; Arguments *args;}){argc, argv, &args}, &ret);

    assert(ret == EINVAL);
    assert(args.compress_mode == true);
    assert(args.extract_mode == true);
    assert(strstr(stderr_output, "mutually exclusive") != NULL);

    free(stderr_output);
    remove(input_path);
    free(input_path);
}

static void test_directory_flag_cleared_for_regular_file(void) {
    char *input_path = create_temp_file("test data");

    char output_template[] = "/tmp/czip_cli_outXXXXXX.huf";
    int out_fd = mkstemps(output_template, 4);
    assert(out_fd >= 0);
    close(out_fd);
    unlink(output_template);

    int argc = 7;
    char *argv[] = {"czip", "-c", "-r", input_path, "-o", output_template, "-f"};

    int ret = huffman_main(argc, argv);
    assert(ret == SUCCESS);

    struct stat st = {0};
    assert(stat(output_template, &st) == 0);

    remove(input_path);
    remove(output_template);
    free(input_path);
}

static void test_directory_input_without_recursive_flag(void) {
    char *dir_path = create_temp_dir();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/nested.txt", dir_path);

    char *nested = create_temp_file("dir content");
    rename(nested, file_path);
    free(nested);

    int argc = 3;
    char *argv[] = {"czip", "-c", dir_path};

    int ret = 0;
    char *stderr_output = capture_stream_output(stderr, invoke_main,
        &(struct {int argc; char **argv;}){argc, argv}, &ret);

    assert(ret == EISDIR);
    assert(strstr(stderr_output, "will not compress a directory without the -r option") != NULL);

    remove(file_path);
    rmdir(dir_path);
    free(stderr_output);
    free(dir_path);
}

int main(void) {
    test_no_input_provided();
    test_missing_output_value();
    test_conflicting_modes();
    test_directory_flag_cleared_for_regular_file();
    test_directory_input_without_recursive_flag();

    printf("test_cli passed.\n");
    return 0;
}


#define _POSIX_C_SOURCE 200809L // NOLINT(bugprone-reserved-identifier): POSIX feature selector
#include "controller.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    FILE *stream = tmpfile();
    assert(stream != NULL);
    assert(dup2(fileno(stream), STDERR_FILENO) >= 0);
    assert(logging_runtime_record(42, 0) == CRUX_OK);
    assert(logging_cpp_record(43, -7) == CRUX_OK);
    assert(logging_controller_state(true) == CRUX_OK);
    assert(logging_controller_state(false) == CRUX_OK);
    assert(fflush(stderr) == 0);
    assert(fseek(stream, 0, SEEK_SET) == 0);
    char output[512] = {0};
    size_t length = fread(output, 1, sizeof(output) - 1, stream);
    assert(length > 0);
    assert(strcmp(output, "INFO runtime.c: sequence=42 code=0\n"
                          "ERROR runtime.cpp: sequence=43 code=-7\n"
                          "INFO controller: healthy\nERROR controller: fault\n") == 0);
    assert(fclose(stream) == 0);
    return 0;
}

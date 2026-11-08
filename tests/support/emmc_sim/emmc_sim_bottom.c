/* SPDX-License-Identifier: Apache-2.0 */
#include "emmc_sim_bottom.h"

#include <errno.h>
#include <fcntl.h>
#include <nsi_tracing.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int image_fd = -1;
static int trace_fd = -1;

void emmc_sim_bottom_store_image(uint32_t offset, const void *data, uint32_t size)
{
    const char *from = data;

    if (image_fd < 0) {
        return;
    }
    while (size > 0) {
        ssize_t written = pwrite(image_fd, from, size, offset);

        if (written <= 0) {
            nsi_print_error_and_exit("emmc_sim: cannot write the image: %s\n", strerror(errno));
        }
        from += written;
        offset += written;
        size -= written;
    }
}

void emmc_sim_bottom_load_image(const char *path, void *data, uint32_t size)
{
    char *to = data;
    uint32_t offset = 0;
    struct stat status;

    image_fd = open(path, O_RDWR);
    if (image_fd < 0 && errno == ENOENT) {
        image_fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (image_fd < 0) {
            nsi_print_error_and_exit("emmc_sim: cannot create %s: %s\n", path, strerror(errno));
        }
        emmc_sim_bottom_store_image(0, data, size);
        return;
    }
    if (image_fd < 0 || fstat(image_fd, &status) != 0) {
        nsi_print_error_and_exit("emmc_sim: cannot open %s: %s\n", path, strerror(errno));
    }
    if (status.st_size != (off_t)size) {
        nsi_print_error_and_exit("emmc_sim: %s has %lld bytes, the medium has %u\n", path,
                                 (long long)status.st_size, size);
    }
    while (offset < size) {
        ssize_t got = pread(image_fd, to + offset, size - offset, offset);

        if (got <= 0) {
            nsi_print_error_and_exit("emmc_sim: cannot read %s: %s\n", path, strerror(errno));
        }
        offset += got;
    }
}

void emmc_sim_bottom_open_trace(const char *path)
{
    trace_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (trace_fd < 0) {
        nsi_print_error_and_exit("emmc_sim: cannot create %s: %s\n", path, strerror(errno));
    }
}

void emmc_sim_bottom_append_trace(const void *data, uint32_t size)
{
    const char *from = data;

    if (trace_fd < 0) {
        return;
    }
    while (size > 0) {
        ssize_t written = write(trace_fd, from, size);

        if (written <= 0) {
            nsi_print_error_and_exit("emmc_sim: cannot write the trace: %s\n", strerror(errno));
        }
        from += written;
        size -= written;
    }
}

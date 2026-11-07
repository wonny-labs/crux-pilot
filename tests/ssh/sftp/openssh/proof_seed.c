/* The entropy of the Linux host, for the proof server only. It says nothing about any device. */
#include <stddef.h>
#include <sys/random.h>

int crux_ssh_sftp_proof_seed(unsigned char *output, unsigned int size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t got = getrandom(output + done, size - done, 0);
        if (got <= 0) {
            return -1;
        }
        done += (size_t)got;
    }
    return 0;
}

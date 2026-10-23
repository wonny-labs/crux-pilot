#include "consumer.h"
#include "file_host.h"
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    char root[] = "/tmp/crux-file-consumer-XXXXXX";
    if (!mkdtemp(root))
        return 100;
    if (crux_file_host_bind(root) != CRUX_FILE_OK)
        return 101;
    int result = file_consumer();
    if (rmdir(root) != 0 && !result)
        result = 102;
    return result;
}

#include "conformance.h"
#include "credential_test.h"
#include "medium_linux.h"

int main(void)
{
    credential_medium_linux_open();
    credential_conformance_run();
    int status = credential_test_finish("credential conformance on the Linux File provider");
    credential_medium_linux_close();
    return status;
}

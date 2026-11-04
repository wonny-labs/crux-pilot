#include "conformance.h"
#include "credential_test.h"

int main(void)
{
    credential_conformance_run();
    return credential_test_finish("credential conformance on the File fake");
}

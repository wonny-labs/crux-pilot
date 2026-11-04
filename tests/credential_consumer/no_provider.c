#include <crux/credential_store.h>

int main(void)
{
    crux_credential_store_info_t info;
    return crux_credential_store_get_info(&info) == CRUX_OK ? 0 : 1;
}

#include <crux/access.h>

int main(void)
{
    return crux_access_init(NULL) == CRUX_ACCESS_OK ? 0 : 1;
}

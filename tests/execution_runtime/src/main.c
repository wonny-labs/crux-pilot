#include "runtime_fixture.h"

int main(void) {
    return crux_runtime_fixture_run() == 0 ? 0 : 1;
}

export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
cd /ws/crux-pilot
A=components/access/src
echo "== host: Access suite for the first account step (gcc, the flags of the CMake targets)"
mkdir -p /tmp/h
# the sync and interrupt ports need _GNU_SOURCE, as their CMake targets set
for f in components/sync/ports/linux/sync_native.c components/interrupt/ports/linux/interrupt_linux.c; do
  gcc -std=c17 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -Icomponents/sync/include -Icomponents/sync/src -Icomponents/sync/ports/linux -Icomponents/base/include -Icomponents/interrupt/src -c $f -o /tmp/h/$(basename $f).o || echo "compile failed: $f"
done
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -UNDEBUG -D_POSIX_C_SOURCE=200809L -DCRUX_ACCESS_TESTING=1 \
  -DCRUX_ACCESS_TEST_COMPOSITION=1 -Itests/access -I$A -Icomponents/access/include -Icomponents/access/adapter \
  -Icomponents/sync/include -Icomponents/sync/src -Icomponents/sync/ports/linux -Icomponents/base/include \
  -Icomponents/interrupt/src -Itests/zephyr/management_rt1060_compile/first_account \
  $A/access.c $A/access_accounts.c $A/access_delay.c $A/access_login.c $A/access_record.c $A/access_session.c \
  tests/access/fake_ports.c tests/access/access_test.c components/sync/src/sync.c \
  /tmp/h/sync_native.c.o /tmp/h/interrupt_linux.c.o \
  tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c \
  tests/bench_first_account/first_account.c -o /tmp/h/t -lpthread 2>&1 | head -20
/tmp/h/t; echo "host suite exit $?"
echo "== the same suite under ASan and UBSan"
gcc -std=c17 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -UNDEBUG -D_POSIX_C_SOURCE=200809L -DCRUX_ACCESS_TESTING=1 \
  -DCRUX_ACCESS_TEST_COMPOSITION=1 -Itests/access -I$A -Icomponents/access/include -Icomponents/access/adapter \
  -Icomponents/sync/include -Icomponents/sync/src -Icomponents/sync/ports/linux -Icomponents/base/include \
  -Icomponents/interrupt/src -Itests/zephyr/management_rt1060_compile/first_account \
  $A/access.c $A/access_accounts.c $A/access_delay.c $A/access_login.c $A/access_record.c $A/access_session.c \
  tests/access/fake_ports.c tests/access/access_test.c components/sync/src/sync.c \
  /tmp/h/sync_native.c.o /tmp/h/interrupt_linux.c.o \
  tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c \
  tests/bench_first_account/first_account.c -o /tmp/h/ts -lpthread 2>&1 | head -20
/tmp/h/ts; echo "sanitized suite exit $?"

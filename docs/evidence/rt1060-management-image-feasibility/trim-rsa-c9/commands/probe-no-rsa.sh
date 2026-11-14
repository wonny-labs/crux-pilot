#!/usr/bin/env bash
# What NO_RSA does to wolfCrypt's own sources, on the host (no Zephyr, no board). Usage: probe-no-rsa.sh WOLFSSL_DIR
# 1. A caller of the RSA API compiles with RSA and is a compile error without it.
# 2. wolfCrypt's rsa.c and the single-precision sources define RSA symbols with RSA and none (or only ECC math) without.
set -u
W=${1:?usage: probe-no-rsa.sh WOLFSSL_DIR}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cat > "$T/call_rsa.c" <<'C'
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/rsa.h>
int make_rsa_key(void)
{
    RsaKey key;
    WC_RNG rng;
    (void)wc_InitRsaKey(&key, NULL);
    return wc_MakeRsaKey(&key, 3072, 65537L, &rng);
}
C
COMMON="-std=c17 -w -I$W -DWOLFSSL_IGNORE_FILE_WARN -DWOLFSSL_KEY_GEN"
for flag in "" "-DNO_RSA"; do
  echo "== caller of wc_InitRsaKey and wc_MakeRsaKey, flags: [$flag]"
  cc $COMMON -Werror=implicit-function-declaration $flag -fsyntax-only "$T/call_rsa.c" 2> "$T/err.txt"; rc=$?
  echo "exit $rc"; grep -m3 'error:' "$T/err.txt" | sed "s#$T/##"
done
# A host nm prints Mach-O names with a leading underscore.
for flag in "" "-DNO_RSA"; do
  echo "== defined symbols of rsa.c, flags: [$flag]"
  cc $COMMON $flag -DWOLFSSL_HAVE_SP_RSA -DWOLFSSL_KEY_GEN -c "$W/wolfcrypt/src/rsa.c" -o "$T/rsa.o" 2> "$T/err2.txt" || head -3 "$T/err2.txt"
  nm -P "$T/rsa.o" 2>/dev/null | awk '$2 !~ /^[Uu]$/ {n++} END {print n+0 " defined symbols"}'
  nm -P "$T/rsa.o" 2>/dev/null | awk '$2 !~ /^[Uu]$/ {print $1}' | sed 's/^_//' | head -3 | sed 's/^/  e.g. /'
done

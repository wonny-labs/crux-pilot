#!/bin/sh
# Trust your organisation's root CA in the Dev Container image. Run by the Dockerfile
# after the package install that brings ca-certificates and openssl, and before every
# download, so that a TLS-inspecting proxy does not break pip, curl, Git or west.
#
# Every *.crt, *.pem and *.cer file in the folder is read. A file holding one or more
# PEM certificates (-----BEGIN CERTIFICATE-----) is split into one certificate per
# file; a binary (DER) file must hold exactly one certificate and is converted to PEM
# in the build stage with openssl (the developer's file is never changed), or skipped
# with a message when the image has no openssl. Each certificate is copied to
# crux-org-<name>[-<n>].crt in the destination, then update-ca-certificates adds it to
# /etc/ssl/certs/ca-certificates.crt. A file that holds a private key, is neither PEM
# nor DER (for example PKCS#7 or PKCS#12), has data after its DER certificate or does
# not parse as a certificate stops the build, and nothing is installed. Files with other extensions
# are skipped with a message; README.md and .gitkeep are skipped silently. With no
# certificate the script installs nothing.
# See .devcontainer/certs/README.md.
#
# Usage: install-org-ca.sh <certificate folder>
# Environment (only tests change them):
#   CRUX_CA_DEST     destination folder (default /usr/local/share/ca-certificates)
#   CRUX_CA_OPENSSL  openssl command (default openssl)
set -eu

src=${1:?usage: install-org-ca.sh <certificate folder>}
dest=${CRUX_CA_DEST:-/usr/local/share/ca-certificates}
openssl=${CRUX_CA_OPENSSL:-openssl}
convert="openssl x509 -inform der -in <file>.cer -out <file>.crt"
readme=".devcontainer/certs/README.md"

if [ ! -d "$src" ]; then
    echo "no organisation CA folder at $src; skipping"
    exit 0
fi
have_openssl=0
if command -v "$openssl" >/dev/null 2>&1; then
    have_openssl=1
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/split" "$stage/ok"
failed=0

refuse() {
    echo "error: .devcontainer/certs/$1: $2 See $readme." >&2
    failed=1
}

for file in "$src"/* "$src"/.[!.]*; do
    [ -f "$file" ] || continue
    name=${file##*/}
    case "$name" in
        README.md | .gitkeep) continue ;;
        *.crt | *.pem | *.cer) ;;
        *)
            echo "warning: skipped $name: only .crt, .pem or .cer files holding a certificate are installed." >&2
            continue
            ;;
    esac
    if grep -q 'PRIVATE KEY-----' "$file"; then
        refuse "$name" "holds a private key. Put only the CA certificate here, never a key."
        continue
    fi
    rm -f "$stage/split/"*
    note=
    if grep -q -e '-----BEGIN CERTIFICATE-----' "$file"; then
        # One certificate per file, Windows line ends removed.
        tr -d '\r' <"$file" | awk -v out="$stage/split/part" '
            /-----BEGIN CERTIFICATE-----/ { n++; f = sprintf("%s-%04d.pem", out, n); inside = 1
                                            print "-----BEGIN CERTIFICATE-----" > f; next }
            inside { print > f }
            /-----END CERTIFICATE-----/ { if (inside) close(f); inside = 0 }'
    elif [ "$have_openssl" -eq 0 ]; then
        echo "warning: skipped $name: it is not PEM and the image has no openssl to convert it from DER." \
             "Convert it yourself with: $convert" >&2
        continue
    elif "$openssl" x509 -inform der -in "$file" -out "$stage/split/part-0001.pem" >/dev/null 2>&1; then
        # openssl reads the first DER object and ignores what follows: encode the result
        # again and require the same bytes, so that no file is installed in part.
        if "$openssl" x509 -in "$stage/split/part-0001.pem" -outform der -out "$stage/again.der" >/dev/null 2>&1 \
            && cmp -s "$file" "$stage/again.der"; then
            note=" (converted from DER)"
        else
            refuse "$name" "is not exactly one DER certificate: it has data after the certificate or more than one object."
            continue
        fi
    else
        refuse "$name" "is neither a PEM nor a DER certificate. PKCS#7 (.p7b) and PKCS#12 (.pfx, .p12) files are not accepted; export the CA alone as X.509."
        continue
    fi
    stem=${name%.*}
    base=$(printf '%s' "$stem" | tr -c 'A-Za-z0-9._-' '_')
    # root.crt and root.pem both give "root": the second keeps its extension in the name.
    for taken in "$stage/ok/crux-org-$base.crt" "$stage/ok/crux-org-$base-1.crt"; do
        if [ -e "$taken" ]; then
            base=${base}_${name##*.}
            break
        fi
    done
    set -- "$stage/split/"part-*.pem
    count=$#
    number=0
    for part do
        number=$((number + 1))
        if [ "$have_openssl" -eq 1 ] && ! "$openssl" x509 -noout -in "$part" >/dev/null 2>&1; then
            refuse "$name" "certificate $number of $count is not a valid certificate."
            continue
        fi
        if [ "$count" -eq 1 ]; then
            target="crux-org-$base.crt"
        else
            target="crux-org-$base-$number.crt"
        fi
        mv "$part" "$stage/ok/$target"
        subject=
        if [ "$have_openssl" -eq 1 ]; then
            subject=": $("$openssl" x509 -noout -subject -in "$stage/ok/$target")"
        fi
        echo "organisation CA from $name$note$subject"
    done
done

if [ "$failed" -ne 0 ]; then
    echo "error: no organisation CA was installed. Fix or remove the files named above, then rebuild the container." >&2
    exit 1
fi
set -- "$stage/ok/"*.crt
if [ ! -f "$1" ]; then
    echo "no organisation CA in .devcontainer/certs/; skipping"
    exit 0
fi
mkdir -p "$dest"
for cert do
    install -m 0644 "$cert" "$dest/"
done
update-ca-certificates

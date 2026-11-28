# Organisation root CA folder

This folder is for networks where a proxy inspects TLS and re-signs every HTTPS connection with your organisation's root CA. The Dev Container image does not trust that CA, so downloads fail with:

~~~text
curl: (60) SSL certificate problem: unable to get local issuer certificate
~~~

Downloads in the image build or the container fail with a certificate error, for example this one, or the same from pip, Git or west (including the downloads of `scripts/bootstrap.sh`: pip, `west update`, `west sdk install`).

## Add the CA

1. Find the CA. On a machine behind the proxy, ask a failing host, such as `pypi.org`, who issued its certificate:

   ~~~sh
   openssl s_client -connect <host>:443 -servername <host> </dev/null | openssl x509 -noout -issuer
   ~~~

   If the issuer is your organisation, you need the root CA at the top of that issuer's chain.
2. Export it. On Windows: run `certmgr.msc`, open **Trusted Root Certification Authorities** > **Certificates**, right-click the CA, **All Tasks** > **Export...**, and choose **Base-64 encoded X.509 (.CER)** or **DER encoded binary X.509 (.CER)**. Do not choose PKCS #7 or PKCS #12 (.PFX): the build refuses them.
3. Copy the file into this folder with the extension `.crt` (for example `cp <path-to>/org-root.cer .devcontainer/certs/org-root.crt`). The build converts a binary (DER) file to PEM in the image and leaves your file unchanged. A DER file must hold exactly one certificate. If the build log says it cannot convert the file, convert it yourself, for example in WSL2:

   ~~~sh
   openssl x509 -inform der -in org-root.cer -out org-root.crt
   ~~~

4. Run **Dev Containers: Rebuild Container**. The build log prints the subject of each CA it installs.

Each developer adds their own file on their own machine. Never commit a certificate. Git ignores every file in this folder except `.gitkeep` and this README, and the image build reads the folder through a bind mount: the image stores the installed CA certificates and no other file from the folder. With no certificate here, the build installs none.

[`install-org-ca.sh`](../install-org-ca.sh) states which files it accepts and which stop the build, such as a file that holds a private key.

## What trusts the CA

The image always sets these variables to the system bundle `/etc/ssl/certs/ca-certificates.crt`, at build time and at run time:

| Variable | Used by |
| --- | --- |
| `SSL_CERT_FILE` | OpenSSL-based clients, including Python's `ssl` module |
| `REQUESTS_CA_BUNDLE` | Python `requests`, which `west sdk install` uses |
| `PIP_CERT` | `pip` |
| `CURL_CA_BUNDLE` | curl |
| `GIT_SSL_CAINFO` | Git, including `west update` |
| `NODE_EXTRA_CA_CERTS` | Node.js, which the VS Code server runs on |

## Known limits

- Docker pulls the base image on your machine, outside the container. If that pull fails with a certificate error, your machine's Docker set-up has to trust the CA.
- The image build reads the folder with a BuildKit bind mount. VS Code Dev Containers and Docker Desktop use BuildKit by default; a builder without BuildKit stops at that step.

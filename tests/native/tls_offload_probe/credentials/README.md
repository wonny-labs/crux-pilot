# Declared test certificate

**Test only. The private key is public, because it is committed here, and it protects nothing.** It lets a `native_sim` TLS test run before the certificate work of [Issue #190](https://github.com/wonny-labs/crux-pilot/issues/190) exists. Whatever uses it names it in its results as the declared test certificate.

| File | What it holds |
| --- | --- |
| `test-server.crt` | Self-signed P-256 certificate, `CN=localhost`, SAN `DNS:localhost` and `IP:127.0.0.1`, for 100 years from November 2026. The host client trusts it. |
| `test-server.key` | Its PKCS #8 private key |
| `test_tls_credentials.h` | The DER form of both, which the image loads |
| `other-ca.crt` | A certificate that did not sign the server's, with no key kept. A client that trusts only this one must refuse the server. |

The CMake file of each image that includes these files refuses any board but `native_sim`.

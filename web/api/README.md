# Management API specification

[`openapi.yaml`](openapi.yaml) describes the management API for the first screens: session, attempt polling,
current user, own password change, status, network settings and reboot. It lets the firmware and frontend
tracks work in parallel, and it depends on no framework.

**Change rule.** A change to the specification is one reviewed PR that the firmware and frontend tracks rebase
onto. Path parameters are part of this: if the server cannot serve them, they become query parameters in one
such change.

Lint it with `scripts/check-management-api.sh`, which needs the Node.js version pinned in
[`lint/package.json`](lint/package.json) and installs the locked tools outside the source tree. The rules
specific to Crux are in [`lint/spectral.yaml`](lint/spectral.yaml).

The sources are the [web design](../../docs/plans/secure-management-web-design.md) and the
[access contract proposal](../../docs/plans/access-contract-proposal.md), with the status proposal, the frontend
design and the gate record as they merge. The access contract replaces the web design's login status codes, and the
frontend design and the status proposal refine its routes.

Every JSON body has named examples for a mock server. A mock server sends the `x-crux-set-cookie` lines of an
example along with it.

# Isolated RT1060 storage candidate

This sample retains a software-only File/Configuration composition for the pinned
`mimxrt1060_evk@B//qspi` image. `main` remains inert. Command submission and private
command prerequisites return `-EACCES`; the qualified elapsed leaf returns `true`.
There is no target enable option or hardware permission.

`crux_candidate_fixed_trial()` is retained for compilation and inspection. Its
private owners use the native Flash API and split bounded reads, page programs
and 4 KiB erases through the existing copied RAM command interface. The original
NOR and FlexSPI devices, and the three owner proxies, have deferred initialization.
Private ownership and command prerequisites precede proxy/device/backend access.
Only completed READ commands publish their exact bounded result, once.

The trial explicitly selects fresh-media provisioning or an existing provisioned
store; mount/read failures never select provisioning or formatting. Settings/NVS
use the existing disposable pinned-source corrections, full-record readiness scan,
CRC, no name cache and no bad-region initialization. LittleFS mounts with
`FS_MOUNT_FLAG_NO_FORMAT`. Mount/unmount writes remain inside maintenance; a
storage fault closes the media fence even during consuming File close.

The fixed storage trial leaves scratch untouched and performs File write/sync/close/read,
two BOOL/U32 keys with four saves, healthy File remount/readback and one-way exit.
It returns success only after unmount, retired handles and both maintenance/native
finish gates. Later persistence is refused; RAM Configuration operations remain
available. Controller startup wiring is deliberately absent while `main` is inert.

`crux_candidate_scratch_trial()` is a separate, explicitly selected destructive
sample-private episode. It initializes only the scratch proxy and performs one
4 KiB erase, one 4-byte program and readback. It never provisions, mounts or saves
File/Settings. Success means scratch readback completed, not storage readiness.
Both success and error consume the native and maintenance lifetimes; neither
normal storage nor scratch can run again. Normal startup/recovery never calls it.
This software workload is distinct from the proposed 16-byte physical first-write
envelope and grants no physical-operation approval.

`tests/native/rt1060_storage_integration` runs the same composition with Zephyr's
Flash simulator as its controlled command leaf. The actual staged RAM dispatcher
and truthful read completion have separate controlled-leaf tests. Existing
maintenance tests exercise healthy backend reboot/readback, normal NVS GC and
uncertain close/sync/error paths. These are software evidence, not physical
persistence, target timing, NOR admission, production layout/provider adoption,
reset/power-loss qualification or M7 concurrent operation.

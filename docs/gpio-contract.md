# Portable GPIO contract

`crux::gpio` provides synchronous access to one statically composed logical
line through `<crux/gpio.h>`. The initial profile supports input configuration
and logical input reads only. The selected Devicetree `gpios` property owns the
controller, pin and active polarity; portable consumers use only
`CRUX_GPIO_LINE_0` and active/inactive values. Output is excluded because the
reviewed consumer requires only a button input and the current EVKB selection is
not a justified output resource.

Calls run in task context and must be serialized by the caller for the line.
A call made from interrupt context returns `CRUX_ERR_NOT_SUPPORTED` where the
provider can detect it (the Zephyr provider does; the Host provider has no
interrupt context and does not check). When such a call also has an invalid
argument, which of the two statuses is returned is unspecified. Calls before the
kernel is running (pre-kernel or early startup) are not supported but are not
detected. Configuration and reads are synchronous. No output, interrupt callback,
event, debounce, ownership handle, runtime discovery or general pin registry is
provided. Reads preserve their output argument on error. Reading before
the first successful configuration returns `CRUX_ERR_NOT_READY`; a later failed
configuration does not undo an earlier success. A call may block as long as the
native driver does, and no latency bound is guaranteed. Null output pointers return
`CRUX_ERR_INVALID_ARGUMENT`; unsupported logical line IDs return
`CRUX_ERR_OUT_OF_RANGE`. Native driver results `-ENOTSUP` and `-ENOSYS` map to
`CRUX_ERR_NOT_SUPPORTED`; every other native driver failure, including
`-EINVAL`, maps to `CRUX_ERR_IO`.

The Host provider is an in-memory test simulation with private input-drive and
failure-injection controls. The Zephyr provider uses `gpio_dt_spec` and native
GPIO drivers. A composition must select the line with the `crux-gpio0`
Devicetree alias; a missing, disabled or malformed selection fails the provider
build. Current software configurations are Zephyr `native_sim/native/64` using
GPIO emulation, the RT1060-EVKB Rev B cross-build using the `user_button`
resource (Zephyr's `sw0` alias), and the RT1170-EVK board revision A Cortex-M7
cross-build, which selects the board's own `user_button` (GPIO13 pin 0). The
cross-builds do not qualify pin routing, electrical behaviour, timing or physical
operation. Neither board is qualified for physical use of this capability.

The representative consumer configures and reads one input. The existing controller M3
`device_input` wrapper remains unchanged in this increment; its native sample
contract and physical validation status are independent of this new public
capability.

The Zephyr tests cannot induce a native driver failure or a controller that is
not ready, because the GPIO emulator has no fault injection. Those statuses are
verified only through the Host test of the native-result mapping and by review of
the provider's use of it.

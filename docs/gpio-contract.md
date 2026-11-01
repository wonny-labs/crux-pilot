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
interrupt context and does not check). Configuration and reads are synchronous. No output, interrupt callback, event,
debounce, ownership handle, runtime discovery or general pin registry is
provided. Reads preserve their output argument on error. Reading before
configuration returns `CRUX_ERR_NOT_READY`. Null output pointers return
`CRUX_ERR_INVALID_ARGUMENT`; unsupported logical line IDs return
`CRUX_ERR_OUT_OF_RANGE`. Native driver results `-ENOTSUP` and `-ENOSYS` map to
`CRUX_ERR_NOT_SUPPORTED`; every other native driver failure, including
`-EINVAL`, maps to `CRUX_ERR_IO`.

The Host provider is an in-memory test simulation with private input-drive and
failure-injection controls. The Zephyr provider uses `gpio_dt_spec` and native
GPIO drivers. A composition must select the line with the `crux-gpio0`
Devicetree alias; a missing, disabled or malformed selection fails the provider
build. Current software configurations are Zephyr `native_sim/native/64` using
GPIO emulation and the RT1060-EVKB Rev B cross-build using the `user_button`
resource (Zephyr's `sw0` alias). This cross-build does not qualify pin routing,
electrical behaviour, timing or physical operation. RT1170 support remains unqualified.

The representative consumer configures and reads one input. The existing controller M3
`device_input` wrapper remains unchanged in this increment; its native sample
contract and physical validation status are independent of this new public
capability.

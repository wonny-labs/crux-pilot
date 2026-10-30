# Portable GPIO contract

`crux::gpio` provides synchronous access to one statically composed logical
line through `<crux/gpio.h>`. The initial profile supports input configuration,
logical input reads, output configuration and logical output writes. The selected
Devicetree `gpios` property owns the controller, pin and active polarity; portable
consumers use only `CRUX_GPIO_LINE_0` and active/inactive values.

Calls run in task context and must be serialized by the caller for the line.
Configuration and read/write are synchronous. No interrupt callback, event,
debounce, ownership handle, runtime discovery or general pin registry is
provided. Reads preserve their output argument on error. Reading before
configuration returns `CRUX_ERR_NOT_READY`; reading an output or writing an input
returns `CRUX_ERR_NOT_SUPPORTED`. Invalid directions and null output pointers
return `CRUX_ERR_INVALID_ARGUMENT`; unsupported logical line IDs return
`CRUX_ERR_OUT_OF_RANGE`. Native driver failures map to `CRUX_ERR_IO`, with
unsupported driver operations mapped to `CRUX_ERR_NOT_SUPPORTED`.

The Host provider is an in-memory test simulation with private input-drive and
output-observation controls. The Zephyr provider uses `gpio_dt_spec` and native
GPIO drivers. A composition must select the line with the `crux-gpio0`
Devicetree alias; a missing, disabled or malformed selection fails the provider
build. Current software configurations are Zephyr `native_sim/native/64` using
GPIO emulation and the RT1060-EVKB Rev B cross-build using its existing SW0
button resource. This cross-build does not qualify pin routing, electrical
behaviour, timing or physical operation. RT1170 support remains unqualified.

The representative consumer configures and reads one input. Output behavior is
covered by Host and native_sim software tests. The existing controller M3
`device_input` wrapper remains unchanged in this increment; its native sample
contract and physical validation status are independent of this new public
capability.

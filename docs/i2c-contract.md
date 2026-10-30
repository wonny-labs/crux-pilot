# I2C contract

`crux_i2c_transfer` performs one synchronous transfer through the controller selected by the
Zephyr composition's `crux-i2c0` alias. The target address is supplied per call and must be a
7-bit address. The portable API has no Zephyr or NXP types.

The current Crux controller sample consumes only a GPIO input and has no SPI/I2C call site. The
pinned RT1060 EVKB board tree exposes an LPI2C controller but does not define a Crux I2C target
consumer. There is therefore no concrete Crux consumer or physical bus route to drive this API
shape today. The `native_sim` fixture exercises the common register-device pattern (write a
register index, then request a repeated-start read) as an enabling contract only; it does not
establish a present application requirement or board support. A future consumer should identify
its actual device contract before selecting a physical controller or expanding this API.

At least one direction must contain data. A write-only call sends one message; a read-only call
reads one message; a call with both directions submits a write followed by a read with
`I2C_MSG_RESTART` requested before the read. The selected Zephyr driver must honor that request for
the call to provide repeated-start semantics. Pinned Zephyr documentation warns that some drivers
may split messages or omit unsupported restart behavior without reporting an error, so a
composition must verify the selected driver before using combined calls. The `native_sim` test
checks the message flags, not physical controller conformance. Each direction is limited to
`UINT32_MAX` bytes, matching Zephyr's native I2C message length field. Calls return only after the
native driver finishes. Buffers are borrowed for the duration of the call and not retained. When
both are present, TX and RX memory regions must not overlap. After an error, read-buffer contents
and write progress at the target are unspecified; native transfers can fail after partial bus
progress, and Crux provides no rollback or completed-byte count.

Crux provides no transfer deadline. Blocking, native error causes, and any lower-level timeout
follow the selected Zephyr driver; nonzero native transfer results map to `CRUX_ERR_IO`. A
controller that is not ready returns `CRUX_ERR_NOT_READY`. Transfer serialization is specific to
the selected provider and driver. Crux adds no lock and makes no concurrency guarantee; separate
calls are not an atomic application-level transaction.

The Host provider is a deterministic loopback fixture: received bytes echo transmitted bytes, and
read-only transfers return zero bytes. It is for portable consumer tests, not a bus model. The
`native_sim` test uses Zephyr's I2C emulation controller and a register-backed emulated target to
exercise writes and combined repeated-start reads. No physical bus is selected by this increment,
and these results do not qualify hardware.

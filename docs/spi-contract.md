# SPI contract

`crux_spi_transceive` exchanges equal-length TX/RX byte buffers with the peripheral selected by
the Zephyr composition's `crux-spi0` alias. Both buffers must be non-null, distinct, and
non-overlapping; length must be greater than zero. Buffers are borrowed through the synchronous
call and are not retained.

One call submits one full-duplex `spi_transceive` operation with one TX buffer and one RX buffer.
The selected peripheral's Devicetree node owns clock frequency, CPOL/CPHA, bit order, chip-select
routing, and related device configuration. The API is byte-oriented and the provider fixes the word
size at 8 bits; word size is not a composition input. Native driver controls chip-select timing within the call. The Zephyr provider
rejects `spi-hold-cs` because this contract has no release operation; an accepted composition
must therefore release CS according to the native driver's normal end-of-transfer behavior. Crux
does not provide multi-call transaction continuity.

Unsupported modes: half-duplex, TI frame format, peripheral (slave) mode, word sizes other than 8
bits, and held chip select. The Zephyr provider rejects held chip select, half-duplex and TI frame
format at build time. Peripheral mode and other word sizes are fixed by the provider, so a
composition cannot select them.

The call returns after the native transfer finishes. Crux provides no deadline, partial-progress
count, rollback, or concurrency guarantee. Native results `-ENOTSUP` and `-ENOSYS` map to
`CRUX_ERR_NOT_SUPPORTED`; every other nonzero native result maps to `CRUX_ERR_IO`. The peripheral
or its chip-select GPIO not being ready maps to `CRUX_ERR_NOT_READY`. After a failure the receive
buffer contents are unspecified, except that a call rejected before the transfer (NULL buffer,
zero length, interrupt context) leaves it untouched. A call made from interrupt context returns
`CRUX_ERR_NOT_SUPPORTED` where the provider can detect it (the Zephyr provider does; the Host
provider has no interrupt context and does not check). When such a call also has an invalid
argument, which of the two statuses is returned is unspecified. Calls before the kernel is
running (pre-kernel or early startup) are not supported but are not detected. Blocking, timeout,
CS behavior, and serialization depend on the selected driver. Crux adds no lock. Buffer overlap is a caller
precondition and is not checked.

The Host provider is a deterministic loopback fixture, not a bus model. The `native_sim` test uses
Zephyr's SPI emulation controller and a test peripheral to verify data exchange, the composed
frequency, mode, bit-order and chip-select values, the provider's fixed 8-bit word size, and native
error mapping. No physical SPI peripheral
is selected. Flash/XIP paths, including FlexSPI, are outside this capability and remain untouched.

The current Crux source tree has no SPI consumer. This slice is an enabling full-duplex transfer
contract and software test, not evidence of an existing application requirement or board support.
A future consumer should identify a concrete peripheral before selecting a physical controller or
expanding the API.

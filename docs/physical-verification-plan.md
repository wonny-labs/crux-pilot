# M6 physical verification plan (GPIO, I2C, SPI)

Status: plan only. No programming, flashing, reset, debug session, wiring change, board rework or
physical result is authorised by this document. Each item below needs explicit approval, one item
at a time, with its fixture and observer named. The software evidence this plan builds on is
described in the [GPIO](gpio-contract.md), [I2C](i2c-contract.md) and [SPI](spi-contract.md)
contracts and in the [test guide](../tests/README.md).

## Scope

- One board: MIMXRT1060-EVKB Rev B, target `mimxrt1060_evk@B//qspi`.
- The RT1170-EVK Rev A1 Cortex-M7 is outside the M6 and M7 scope by the owner's decision of
  1 November 2026, so that work concentrates on the platform on a single board. Its cross-build and
  the [identification record](rt1170-evk-a1-feasibility.md) remain as software evidence only. No
  physical RT1170 item is planned.
- Evidence is limited to the documented firmware, wiring and parts. A pass on one fixture is not a
  claim about other peripherals, other pins or other boards.

## What the software evidence already covers

| Layer | Covers | Does not cover |
|---|---|---|
| Host | Contract rules, argument checks, status propagation, native-free consumption, the two test consumers (TMP117 for I2C, flash identification for SPI) against test-only providers | Zephyr or any bus |
| `native_sim` | The Zephyr providers against Zephyr's emulators, Devicetree-selected settings, native error mapping, build-time rejection of bad compositions | Real drivers, timing, signals, chip-select behaviour, controllers that are not ready |
| Board cross-build | The portable GPIO consumer compiles and links for this board | That a pin, bus or peripheral works |

The device models in the test consumers (TMP117 registers, the `0x9F` flash command) were written
from data sheets as read. No physical TMP117 or flash has been used. The checks below are the first
comparison against real parts.

## Facts used from the board description

Read from the board's Devicetree at the pinned Zephyr revision (`684c9e8f`); each item below
records them again as a precondition to confirm, not as an assumption.

| Item | Pinned Devicetree |
|---|---|
| User button | `user_button` is "User SW8" on `gpio5` pin 0, `GPIO_PULL_UP \| GPIO_ACTIVE_LOW` |
| Arduino I2C | `arduino_i2c` is `lpi2c1` (SDA pad `GPIO_AD_B1_01`, SCL pad `GPIO_AD_B1_00`); the same controller is named for the touch and camera connectors |
| Arduino SPI | `arduino_spi` is `lpspi1` (pads `GPIO_SD_B0_00` to `GPIO_SD_B0_03`, chip select `PCS0`). The board file states that these pads conflict with the microSD (USDHC) pins and that resistors R278, R279, R280 and R281 must be connected on the board for SPI |
| Second SPI | `lpspi3` is enabled but conflicts with LCD interface pins |

Consequences: the SPI items need a board modification or a different pin set, and an enabled
`lpspi1` takes the microSD pins away from the M4 file-storage path. Both must be decided before P3.
Arduino header pin numbers are not stated here; take them from the board's user manual and record
the document and revision used.

## Common rules for every item

1. **Identification.** Record the board revision from the silkscreen, the exact commit, the SHA-256
   of the image and the toolchain. A result without these is not usable evidence.
2. **Approval before action.** Programming, reset, debug probe use and any wiring or rework are
   separate approvals. Do not use the debug probe to halt, reset or write memory unless that item
   says so and is approved.
3. **No automatic retry.** A failed or inconclusive run stops. Repeats are new runs with a new
   record.
4. **Observers are independent.** The target's own log is a witness, not the measurement. Where an
   item names an external observer, a pass needs the external record.
5. **Logic levels and power.** Fixtures are 3.3 V. No 5 V part is connected to a pad. Pull-up
   values and the supply for each module are recorded. Power is removed before wiring changes.
6. **Console.** Results are captured from the board's serial console with host receipt time. Target
   and host clocks are separate domains; correlate events, never subtract across them.
7. **A software preparation step precedes each item.** The physical application, its overlay and
   its Devicetree alias must exist and build in CI before the item is approved. They are listed per
   item.

## P1 - GPIO input: routing and polarity

**Question.** Does the portable GPIO consumer, bound to `user_button`, report the logical value
that the physical button state implies on this board?

**Software preparation.** A small application using the existing portable GPIO consumer that reads
the line periodically and prints value and monotonic timestamp. The existing board overlay selects
`user_button`.

**Fixture and observer.** The board only. Observer: the serial log plus a written operator log of
when SW8 was pressed and released. An optional multimeter or oscilloscope on the pin confirms the
electrical level.

**Procedure.** After the approved programming step, record 10 intervals with SW8 released and 10
with SW8 held, at least 2 s each, with the operator log marking the changes.

**Pass.** The logical value is inactive while released and active while held, in all 20 intervals,
consistent with the pull-up and active-low description above. The optional electrical observation
shows high released and low held.

**Inconclusive.** Missing operator log, a log without the image identification, or any unexpected
reset.

**Does not show.** Other pins, output behaviour (there is no output API), interrupts or debounce.

## P2 - I2C: repeated start with a real sensor

**Question.** Does the native LPI2C driver issue a write followed by a read as one repeated-start
transaction, and does a real TMP117 answer as the consumer's model assumes?

**Software preparation.** A physical application that calls `tmp117_probe` and
`tmp117_read_millidegrees` at the sensor's address, prints status, device ID bits and the
temperature, and repeats at a slow fixed interval. A composition overlay that selects `lpi2c1` and
sets `crux-i2c0` for the board.

**Fixture.** A TMP117 breakout module on the Arduino I2C pins at 3.3 V. Record the module, its
address strap, its pull-up resistors and whether it adds its own pull-ups. The module's address and
the board's other users of `lpi2c1` (touch and camera connectors) are recorded; nothing else is
connected to these lines during the item.

**Observer.** A logic analyser on SDA and SCL decoding I2C. The target log is a witness only.

**Procedure.** Capture at least 20 reads. For each, the analyser record is checked for: address
write acknowledged, one pointer byte, a repeated start with no stop between the write and the read,
address read acknowledged, two data bytes, then stop.

**Pass.**
1. The device ID read returns identification 0x117 in its low 12 bits.
2. Every captured transaction has a repeated start and no stop between the two parts.
3. The reported temperature is plausible for the room and changes in the expected direction when the
   sensor is warmed by hand, and the value converted from the captured bytes equals the value the
   target printed.
4. No bus error status in the run.

**Fail with finding.** A stop followed by a start between the two parts means the driver split the
combined transfer: the contract's "driver must honour repeated start" is not met on this driver,
and the finding is recorded, not worked around.

**Also record, without a pass criterion.** The measured clock frequency, and what status the
consumer returns with the sensor unplugged (expected `CRUX_ERR_IO`), including how long the call
took to return.

**Does not show.** Behaviour with other targets, clock stretching, bus recovery or multi-controller
operation.

## P3 - SPI: data path

**Question.** Does the native LPSPI driver move bytes full-duplex through the Crux provider with
the composed settings?

**Preconditions to decide first.** The pad conflict with the microSD pins and the R278 to R281
rework described above, or a different pin set. The decision, with the rework approval if chosen,
is recorded before this item is approved.

**Software preparation.** A physical application that sends a fixed byte pattern through
`crux_spi_transceive` and compares the received bytes; an overlay with a loopback peripheral node.

**Fixture.** One wire from MOSI to MISO on the header. No peripheral.

**Observer.** The target log, plus a logic analyser on SCK, MOSI, MISO and chip select.

**Procedure.** At least 20 exchanges of lengths 1, 4 and 64 bytes with patterns that include
`0x00`, `0xFF`, `0xA5` and an incrementing sequence.

**Pass.** Received bytes equal sent bytes in every exchange, and the analyser shows the composed
mode and clock for every exchange (see P4 for the signal checks).

**Does not show.** That a peripheral on the bus interprets the settings the same way; a loopback
cannot distinguish clock modes.

## P4 - SPI: command and response with a real flash, mode and chip select

**Question.** Does one `crux_spi_transceive` hold chip select across the command and the response,
and does a real serial NOR flash answer the identification command as the consumer assumes?

**Software preparation.** A physical application that calls `spi_flash_read_jedec_id` and prints the
result and capacity. The overlay sets the clock and mode that the chosen part's data sheet allows.

**Fixture.** A serial NOR flash breakout at 3.3 V on the SPI pins. Record the part number, its data
sheet revision and whether write protect and hold pins are tied as the data sheet requires. If the
part has continuation codes or a different capacity encoding, the consumer's decoding is expected
to differ and that is recorded as a finding.

**Observer.** A logic analyser on SCK, MOSI, MISO and chip select, decoding SPI.

**Procedure.** Capture at least 20 reads, then the same with the flash disconnected.

**Pass.**
1. Chip select falls once, before the command byte, and rises once, after the third response byte,
   in every capture.
2. The command byte is `0x9F`; mode, bit order and clock match the composition.
3. The decoded manufacturer, type and capacity bytes equal the part's data sheet and the values the
   target printed.
4. With the flash disconnected the consumer returns `CRUX_ERR_IO` (idle line), not an
   identification.

**Fail with finding.** Chip select toggling between bytes would contradict the SPI contract and is
recorded, not worked around.

**Does not show.** Reads, writes or erases of memory; behaviour with other parts; throughput.

## Order and gating

1. Decide the pad and rework question for SPI and the TMP117 and flash modules. Record the choices.
2. Prepare the software for P1 and P2 (and P3, P4 after step 1) and merge it with CI passing.
3. Approve and run P1, then P2, one at a time, recording results in a new document.
4. Approve and run P3, then P4, under the same rule.

None of these steps closes Issue #31 or #32. They are the evidence needed for criterion 4 of Issue
#31 on this board.

## Open decisions

- Which TMP117 module and which flash part to buy or borrow.
- Whether to rework the board for `lpspi1` (conflicts with the microSD pins) or choose another SPI
  pin set.
- Whether a logic analyser is available, and its decoder.
- Who performs the approved physical steps, and where the records are kept.

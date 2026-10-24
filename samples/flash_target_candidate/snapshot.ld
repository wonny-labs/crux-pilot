/* Dedicated OCRAM2, independent of initial FlexRAM partition. Candidate only. */
SECTION_PROLOGUE(.pilot_snapshot,(NOLOAD),)
{
    . = ALIGN(64);
    KEEP(*(.pilot_snapshot))
} GROUP_LINK_IN(OCRAM2)
ASSERT(pilot_reset_snapshot == 0x20200000, "snapshot must use reserved OCRAM2 start")
ASSERT(pilot_reset_snapshot_end - pilot_reset_snapshot == 64, "snapshot size mismatch")
ASSERT(pilot_reset_snapshot_end <= 0x20280000, "snapshot outside dedicated OCRAM2")

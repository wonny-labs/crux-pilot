/* Sample-only proposed limit includes ROM headers and relocated RAM load bytes. */
ASSERT(_flash_used <= 0x620000, "Pilot candidate firmware exceeds protected envelope")

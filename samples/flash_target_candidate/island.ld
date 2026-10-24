/* Candidate-only independent sections: copied and compared before first entry. */
SECTION_PROLOGUE(.candidate_island_text,,)
{
    . = ALIGN(4);
    __island_text_start = .;
    KEEP(*(.candidate_island_text))
    __island_text_end = .;
} GROUP_DATA_LINK_IN(ITCM, FLASH)
__island_text_load = LOADADDR(.candidate_island_text);
SECTION_PROLOGUE(.candidate_island_vectors,,)
{
    . = ALIGN(1024);
    __island_vectors_start = .;
    KEEP(*(.candidate_island_vectors))
    __island_vectors_end = .;
} GROUP_DATA_LINK_IN(DTCM, FLASH)
__island_vectors_load = LOADADDR(.candidate_island_vectors);
SECTION_PROLOGUE(.candidate_island_stacks,(NOLOAD),)
{
    . = ALIGN(8);
    __island_operation_bottom = .;
    . += 1024;
    __island_operation_top = .;
    __island_exception_bottom = .;
    . += 1024;
    __island_exception_top = .;
} GROUP_LINK_IN(DTCM)
ASSERT(__island_text_end <= 0x20000, "island outside ITCM")
ASSERT(__island_exception_top <= 0x20020000, "island outside DTCM")
ASSERT((__island_vectors_start & 1023) == 0, "vector alignment")
ASSERT(__island_vectors_end - __island_vectors_start == 1024, "vector size")

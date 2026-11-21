# Candidate for issue #165: Not built, Not verified, not for merge.
#
# Moves the const FlexSPI configuration object into the memory that already holds the FlexSPI
# driver's code, so the NOR busy poll does not read its register base from the NOR it is waiting
# on. At the pinned Zephyr (v4.4.2, read in source, Not verified in a build),
# drivers/memc/CMakeLists.txt relocates the text of memc_mcux_flexspi.c only, and
# relocates both the text and the read-only data of its xSPI sibling. This adds the read-only data
# of the one object, with Zephyr's code relocation:
# scripts/build/gen_relocate_app.py matches FILTER against section names, so it needs
# -fdata-sections to give the object a section of its own (Zephyr's default, not checked in a
# build). It moves no other object.
if(CONFIG_CRUX_BOOT_FLEXSPI_CONFIG_IN_RAM)
    if(NOT CONFIG_MEMC_MCUX_FLEXSPI OR NOT CONFIG_FLASH OR NOT CONFIG_FLASH_MCUX_FLEXSPI_XIP
       OR NOT CONFIG_CODE_DATA_RELOCATION)
        message(FATAL_ERROR "CONFIG_CRUX_BOOT_FLEXSPI_CONFIG_IN_RAM needs CONFIG_MEMC_MCUX_FLEXSPI, CONFIG_FLASH, "
                            "CONFIG_FLASH_MCUX_FLEXSPI_XIP and CONFIG_CODE_DATA_RELOCATION: the "
                            "relocation it asks for is the one that the FlexSPI XIP driver already "
                            "uses for its code")
    endif()
    # A character class instead of backslashes keeps the pattern intact through the relocation
    # directive, which Zephyr writes into a file as text.
    zephyr_code_relocate(
        FILES "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c"
        LOCATION "${CONFIG_FLASH_MCUX_FLEXSPI_XIP_MEM}_RODATA"
        FILTER "^[.]rodata[.]memc_flexspi_config_"
    )
endif()

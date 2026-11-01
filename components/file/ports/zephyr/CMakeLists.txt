get_filename_component(CRUX_FILE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT CONFIG_BOARD_NATIVE_SIM OR
   NOT (CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR))
  message(FATAL_ERROR "Crux File native provider requires an approved simulator composition")
endif()
if(CONFIG_CRUX_FILE_SIMULATOR AND CONFIG_CRUX_FILE_FATFS_SIMULATOR)
  message(FATAL_ERROR "Crux File simulator compositions must be mutually exclusive")
endif()
if(CONFIG_CRUX_FILE_FATFS_SIMULATOR AND
   (NOT CONFIG_FS_FATFS_LFN OR CONFIG_FS_FATFS_MAX_LFN LESS 127 OR
    NOT CONFIG_FS_FATFS_REENTRANT OR CONFIG_FS_FATFS_MOUNT_MKFS))
  message(FATAL_ERROR "FatFs simulator requires LFN >= 127, reentrancy and auto-format disabled")
endif()

add_library(crux_file_api INTERFACE)
target_include_directories(crux_file_api INTERFACE "${CRUX_FILE_COMPONENT_DIR}/include")
# Match native_sim's non-PIE CPU image, without kernel headers/definitions.
target_compile_options(crux_file_api INTERFACE ${TOOLCHAIN_C_FLAGS} -fno-pic -fno-pie)
# native_sim's width is a compiler ABI requirement, not a kernel API requirement.
if(CONFIG_64BIT)
  target_compile_options(crux_file_api INTERFACE -m64)
else()
  target_compile_options(crux_file_api INTERFACE -m32)
endif()

add_library(crux_file_zephyr STATIC "${CRUX_FILE_COMPONENT_DIR}/src/file.c" file_native.c)
target_include_directories(crux_file_zephyr PRIVATE "${CRUX_FILE_COMPONENT_DIR}/src" .)
zephyr_append_cmake_library(crux_file_zephyr)
target_link_libraries(crux_file_zephyr PRIVATE crux_file_api zephyr_interface)
target_compile_options(crux_file_zephyr PRIVATE -Wall -Wextra -Werror)

add_library(crux_file INTERFACE)
add_library(crux::file ALIAS crux_file)
target_link_libraries(crux_file INTERFACE crux_file_api)

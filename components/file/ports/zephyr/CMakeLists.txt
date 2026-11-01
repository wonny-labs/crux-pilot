get_filename_component(CRUX_FILE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK)
  if(CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_BOARD_MIMXRT1060_EVK OR
     NOT CONFIG_SOC_MIMXRT1062 OR NOT CONFIG_FAT_FILESYSTEM_ELM OR
     NOT CONFIG_FILE_SYSTEM OR NOT CONFIG_DISK_DRIVER_SDMMC OR
     NOT CONFIG_IMX_USDHC OR NOT CONFIG_FS_FATFS_LFN OR
     CONFIG_FS_FATFS_MAX_LFN LESS 127 OR NOT CONFIG_FS_FATFS_REENTRANT OR
     CONFIG_FS_FATFS_MOUNT_MKFS)
    message(FATAL_ERROR "RT1060 File check requires the selected FatFs/SDHC build with auto-format disabled")
  endif()
  if(CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR)
    message(FATAL_ERROR "RT1060 compile check cannot be combined with simulator providers")
  endif()

  # Compile only: do not register the unqualified native File provider in the image.
  add_library(crux_file_fatfs_rt1060_compile OBJECT EXCLUDE_FROM_ALL
      "${CRUX_FILE_COMPONENT_DIR}/src/file.c" file_native.c)
  target_include_directories(crux_file_fatfs_rt1060_compile PRIVATE
      "${CRUX_FILE_COMPONENT_DIR}/include"
      "${CRUX_FILE_COMPONENT_DIR}/src"
      ".")
  target_link_libraries(crux_file_fatfs_rt1060_compile PRIVATE zephyr_interface)
  target_compile_options(crux_file_fatfs_rt1060_compile PRIVATE -Wall -Wextra -Werror)
  add_custom_target(crux_file_target_compile_only ALL)
  add_dependencies(crux_file_target_compile_only crux_file_fatfs_rt1060_compile)
  return()
endif()

if(CONFIG_CRUX_FILE_FATFS_RT1060)
  if(CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR)
    message(FATAL_ERROR "The RT1060 provider cannot be combined with simulator providers")
  endif()
  add_library(crux_file_api INTERFACE)
  target_include_directories(crux_file_api INTERFACE "${CRUX_FILE_COMPONENT_DIR}/include")
  add_library(crux_file_zephyr STATIC "${CRUX_FILE_COMPONENT_DIR}/src/file.c" file_native.c)
  target_include_directories(crux_file_zephyr PRIVATE "${CRUX_FILE_COMPONENT_DIR}/src" .)
  zephyr_append_cmake_library(crux_file_zephyr)
  target_link_libraries(crux_file_zephyr PRIVATE crux_file_api zephyr_interface)
  target_compile_options(crux_file_zephyr PRIVATE -Wall -Wextra -Werror)
  add_library(crux_file INTERFACE)
  add_library(crux::file ALIAS crux_file)
  target_link_libraries(crux_file INTERFACE crux_file_api)
  return()
endif()

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
if(CONFIG_BOARD_NATIVE_SIM)
  if(CONFIG_64BIT)
    target_compile_options(crux_file_api INTERFACE -m64)
  else()
    target_compile_options(crux_file_api INTERFACE -m32)
  endif()
endif()

add_library(crux_file_zephyr STATIC "${CRUX_FILE_COMPONENT_DIR}/src/file.c" file_native.c)
target_include_directories(crux_file_zephyr PRIVATE "${CRUX_FILE_COMPONENT_DIR}/src" .)
zephyr_append_cmake_library(crux_file_zephyr)
target_link_libraries(crux_file_zephyr PRIVATE crux_file_api zephyr_interface)
target_compile_options(crux_file_zephyr PRIVATE -Wall -Wextra -Werror)

add_library(crux_file INTERFACE)
add_library(crux::file ALIAS crux_file)
target_link_libraries(crux_file INTERFACE crux_file_api)

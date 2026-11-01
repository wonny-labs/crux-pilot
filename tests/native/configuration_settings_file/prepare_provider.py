#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a test-only provider copy; never alter the production guard."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
start = source.index('#if !defined(CRUX_CONFIGURATION_SETTINGS_SIMULATOR)')
end = source.index('\n#endif', start) + len('\n#endif')
old = source[start:end]
if 'CONFIG_FLASH_SIMULATOR' not in old or 'CONFIG_SETTINGS_NVS' not in old:
    raise SystemExit('Unexpected provider guard; refusing to prepare copy')
new = '''#if !defined(CONFIG_BOARD_NATIVE_SIM) || !defined(CONFIG_SETTINGS_FILE)
#error "This copy is only for the Settings File native_sim experiment"
#endif'''
copy = source[:start] + new + source[end:]

# Prototype of a Crux-side mitigation, off unless a test enables it: read each saved record back.
write_old = "    return result(settings_save_one(name, buffer, length));\n"
if copy.count(write_old) != 1:
    raise SystemExit('Unexpected provider write; refusing to prepare copy')
write_new = """    crux_config_result_t saved = result(settings_save_one(name, buffer, length));
    if (saved == CRUX_CONFIG_OK && crux_probe_read_back) {
        uint8_t check[RECORD_CAPACITY];
        crux_config_record_info_t info;
        crux_config_result_t read = crux_config_record_read(key, check, sizeof(check), &info);
        if (read != CRUX_CONFIG_OK || info.length != length ||
            memcmp(check, buffer, length) != 0) {
            return CRUX_CONFIG_ERR_IO; /* acknowledged by the backend, but not readable */
        }
    }
    return saved;
"""
copy = copy.replace(write_old, write_new)
marker = "static bool initialized;\n"
if copy.count(marker) != 1:
    raise SystemExit('Unexpected provider state; refusing to prepare copy')
copy = copy.replace(marker, marker + "bool crux_probe_read_back;\n")
Path(sys.argv[2]).write_text(copy)

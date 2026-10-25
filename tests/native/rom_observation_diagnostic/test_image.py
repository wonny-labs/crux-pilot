"""Exact diagnostic ARM closure. No Cortex-M execution or hardware qualification."""
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[3]

@unittest.skipUnless(os.environ.get('PILOT_ELF'), 'requires the exact diagnostic ARM image')
class Image(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('elf_symbols', ROOT / 'tests/native/flash_target_candidate/assess_image.py')
        cls.image = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.image)
        cls.elf = Path(os.environ['PILOT_ELF']).read_bytes()
        cls.symbols = cls.image.symbols(cls.elf)
        cls.disassembly = subprocess.check_output([os.environ['ARM_OBJDUMP'], '-d', os.environ['PILOT_ELF']], text=True)
        cls.bodies = {match[1]: match[2] for match in re.finditer(r'^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)', cls.disassembly, re.M | re.S)}
        cls.directory = Path(os.environ['PILOT_ELF']).parent

    def bytes_at(self, address, size):
        header = struct.unpack_from('<16sHHIIIIIHHHHHH', self.elf)
        for i in range(header[10]):
            segment = struct.unpack_from('<8I', self.elf, header[5] + i * header[9])
            if segment[0] == 1 and segment[2] <= address and address + size <= segment[2] + segment[4]:
                return self.elf[segment[1] + address - segment[2]:segment[1] + address - segment[2] + size]
        self.fail('range is not file backed: ' + hex(address))

    def test_no_native_flash_storage_reset_or_write_capability_in_image(self):
        forbidden = ['flash_flexspi_nor_init', 'memc_flexspi_init', 'crux_rom_command_prepare',
                     'crux_rom_command_submit', 'crux_rom_command_read_result', 'memc_flexspi_reset',
                     'FLEXSPI_SoftwareReset', 'FLEXSPI_WriteBlocking', 'flash_flexspi_nor_write',
                     'crux_rom_native_write_enable', 'crux_rom_native_page_program',
                     'crux_rom_native_erase_sector', 'fs_mount', 'fs_mkfs', 'settings_subsys_init',
                     'nvs_mount', 'sys_reboot', 'wdt_feed', 'crux_system_reset', 'crux_system_init']
        for name in forbidden:
            self.assertFalse(name in self.symbols, name)
        setter = self.bodies['flexspi_clock_set_freq']
        self.assertNotRegex(setter, r'\b(?:bl|blx|ldr|str)\b')
        self.assertRegex(setter, r'mvn\.w\s+r0, #12')
        config = (self.directory / '.config').read_text()
        for name in ['FLASH', 'FILE_SYSTEM', 'SETTINGS', 'NVS', 'WATCHDOG', 'HWINFO', 'REBOOT',
                     'NETWORKING', 'DMA', 'SHELL', 'CONSOLE', 'LOG', 'PRINTK', 'FPU', 'CRUX_SYSTEM']:
            self.assertNotRegex(config, r'(?m)^CONFIG_' + name + '=y$', name)
        commands = json.loads((self.directory.parent / 'compile_commands.json').read_text())
        for entry in commands:
            self.assertNotRegex(entry['file'], r'(?:fsl_gpt\.c|storage_native\.c|storage_adapter\.c|storage_trial\.c|flash_mcux_flexspi_nor\.c)$')

    def test_owned_ram_sources_have_scoped_compiler_warning_policy(self):
        commands = json.loads((self.directory.parent / 'compile_commands.json').read_text())
        owned = ['samples/rom_observation_diagnostic/observation.c',
                 'samples/rom_observation_diagnostic/terminal.c',
                 'samples/flash_target_candidate/rom_deadline.c',
                 'samples/rom_observation_diagnostic/main.c',
                 'samples/rom_observation_diagnostic/runtime.c']
        for relative in owned:
            matches = [entry for entry in commands if entry['file'].endswith('/' + relative)]
            self.assertEqual(len(matches), 1, relative)
            flags = shlex.split(matches[0]['command'])
            for flag in ['-Wall', '-Wextra', '-Werror']:
                self.assertIn(flag, flags, relative + ' must enforce ' + flag)
            self.assertNotIn('-Wno-error', flags, relative)
        # Do not impose the owned-code policy on pinned/staged native SDK code.
        for name in ['observation_native.c', 'memc_mcux_flexspi.c', 'fsl_flexspi.c']:
            matches = [entry for entry in commands if Path(entry['file']).name == name]
            self.assertEqual(len(matches), 1, name)
            self.assertNotIn('-Werror', shlex.split(matches[0]['command']), name)

    def test_selected_code_literals_and_conservative_stack_close_in_ram(self):
        pending = ['crux_rom_command_dispatch']
        seen = set()
        starts = {name: int(re.search(r'^\s*([0-9a-f]+):', body, re.M)[1], 16) for name, body in self.bodies.items()}
        while pending:
            name = pending.pop()
            if name in seen:
                continue
            seen.add(name)
            self.assertIn(name, self.bodies)
            self.assertLess(self.symbols[name] & ~1, 0x20000, name)
            body = self.bodies[name]
            self.assertNotRegex(body, r'\bv(?:push|pop|ldr|str|mov|add|sub|mul|div)')
            self.assertNotRegex(body, r'\bbl\s+[^<]*<' + re.escape(name) + r'>', 'recursive selected call')
            self.assertNotRegex(body, r'\bblx\s+(?:r\d+|ip|sb|sl|fp)|\bbx\s+(?:r(?:[0-9]|1[0-2])|ip|sb|sl|fp)\b|\b(?:ldr|mov)\s+pc,')
            addresses = [int(value, 16) for value in re.findall(r'^\s*([0-9a-f]+):', body, re.M)]
            for target, annotation in re.findall(r'\b(?:bl|b(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.[nw])?|cbz|cbnz)\s+(?:r\d+,\s*)?([0-9a-f]+)\s+<([^>]+)>', body):
                address = int(target, 16)
                if min(addresses) <= address <= max(addresses):
                    continue
                self.assertLess(address, 0x20000, (name, annotation))
                matches = [key for key, value in starts.items() if value == address]
                self.assertEqual(len(matches), 1, (name, annotation))
                pending.append(matches[0])
            for word in re.findall(r'\.word\s+0x([0-9a-f]+)', body):
                value = int(word, 16)
                self.assertFalse(0x60000000 <= value < 0x60800000, (name, word))
                if 0x20000000 <= value < 0x20020000:
                    self.assertTrue(any(self.symbols[f'__dtcm_{kind}_reloc_start'] <= value < self.symbols[f'__dtcm_{kind}_reloc_start'] + self.symbols[f'__dtcm_{kind}_reloc_size'] for kind in ['rodata', 'data', 'bss']), (name, word))
        frames = {}
        for name in seen:
            body = self.bodies[name]
            self.assertNotRegex(body, r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*r\d+')
            frame = sum(4 * len(registers.split(',')) for registers in re.findall(r'\b(?:push|stmdb)\s+(?:sp!,\s*)?\{([^}]+)\}', body))
            frame += sum(map(int, re.findall(r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*#(\d+)', body)))
            frames[name] = frame
        self.assertLessEqual(sum(frames.values()), 1024)
        for name in ['crux_rom_identity_check', 'crux_rom_identity_restore', 'FLEXSPI_UpdateLUT',
                     'memc_flexspi_transfer', 'FLEXSPI_TransferBlocking', 'crux_rom_candidate_wait_check',
                     'crux_rom_candidate_deadline_expired', 'crux_flash_poc_terminal']:
            self.assertIn(name, seen)
        if os.environ.get('OBSERVATION_CLOSURE_JSON'):
            Path(os.environ['OBSERVATION_CLOSURE_JSON']).write_text(json.dumps({
                'functions': {name: {'address': self.symbols[name], 'frame_upper_bytes': frames[name]} for name in sorted(seen)},
                'all_frames_conservative_sum': sum(frames.values()), 'operation_stack_bytes': 1024,
                'scope': 'exact selected software graph/literals/stack; MMIO can stall; no hardware execution'}, indent=2) + '\n')

    def test_record_metadata_and_full_lut_are_internal_ram(self):
        for name in ['crux_observation_result', 'crux_observation_nor', 'crux_rom_memc_config',
                     'crux_rom_memc_data', 'crux_rom_deadline', 'crux_rom_slot',
                     'crux_rom_expected_lut', 'crux_rom_id_lut']:
            self.assertGreaterEqual(self.symbols[name], 0x20000000, name)
            self.assertLess(self.symbols[name], 0x20020000, name)
        record = struct.unpack('<4I', self.bytes_at(self.symbols['crux_observation_result'], 16))
        self.assertEqual(record[:2], (0x4f425356, 1))
        self.assertEqual(record[3], 0)
        self.assertLess(self.symbols['crux_observation_result'] + record[2], 0x20020000)
        expected = self.bytes_at(self.symbols['crux_rom_expected_lut'], 256)
        original = Path(os.environ['PILOT_ORIGINAL_FCB']).read_bytes()
        self.assertEqual(expected, original[0x80:0x180])
        binary = Path(os.environ['PILOT_BIN']).read_bytes()
        self.assertEqual(binary[0x80:0x180], expected)
        self.assertEqual(binary[0x46], 6)
        self.assertEqual([(i, left, right) for i, (left, right) in enumerate(zip(original[:512], binary[:512])) if left != right], [(0x46, 7, 6)])
        self.assertEqual(binary[0x10], 0, 'FCB must not request ROM mode commands')
        self.assertEqual(binary[0x1c], 0, 'FCB must not request ROM config commands')

    def test_vector_stack_context_order_and_refusal(self):
        self.assertEqual(self.symbols['__island_vectors_start'] % 1024, 0)
        words = struct.unpack('<256I', self.bytes_at(self.symbols['__island_vectors_start'], 1024))
        self.assertEqual(words[0], self.symbols['__island_exception_top'])
        self.assertEqual(words[1:], (self.symbols['crux_island_exception_terminal'] | 1,) * 255)
        for kind in ['operation', 'exception']:
            self.assertEqual(self.symbols[f'__island_{kind}_top'] - self.symbols[f'__island_{kind}_bottom'], 1024)
        body = self.bodies['crux_island_enter']
        self.assertEqual(re.findall(r'\bbl\s+[^<]*<([^>]+)>', body), ['crux_rom_command_dispatch'])
        self.assertLess(body.lower().index('msr\tpsp'), body.index('<crux_rom_command_dispatch>'))
        self.assertRegex(body, r'<crux_rom_command_dispatch>[\s\S]*cmp\s+r0, #0[\s\S]*bne[^\n]*<crux_island_exception_terminal>')
        self.assertRegex(body, r'msr\s+MSP, r5[\s\S]*msr\s+CONTROL, r7[\s\S]*msr\s+PSP, r6[\s\S]*msr\s+PRIMASK, r0')
        terminal = self.bodies['crux_island_exception_terminal']
        self.assertNotRegex(terminal, r'\b(push|pop|bl|blx|ldr|str)\b|\bsp\b')
        self.assertIn('cpsid', terminal)

    def test_relocation_copy_load_bytes_and_startup_verifier_order(self):
        binary = Path(os.environ['PILOT_BIN']).read_bytes()
        for prefix in ['itcm_text', 'dtcm_rodata', 'dtcm_data']:
            address = self.symbols[f'__{prefix}_reloc_start']
            length = self.symbols[f'__{prefix}_reloc_size']
            load = self.symbols[f'__{prefix}_rom_start']
            self.assertGreater(length, 0)
            self.assertEqual(length % 4, 0)
            self.assertGreaterEqual(load, 0x60000000)
            self.assertLessEqual(load + length, 0x60620000)
            self.assertEqual(self.bytes_at(address, length), binary[load - 0x60000000:load - 0x60000000 + length])
        entry = self.symbols['__init_crux_island_relocation_init']
        self.assertGreaterEqual(entry, self.symbols['__init_PRE_KERNEL_1_start'])
        self.assertLess(entry, self.symbols['__init_PRE_KERNEL_2_start'])
        prep = self.bodies['z_prep_c']
        self.assertLess(prep.index('<arch_bss_zero>'), prep.index('<arch_data_copy>'))
        self.assertLess(prep.index('<arch_data_copy>'), prep.index('<z_cstart>'))
        self.assertIn('<bss_zeroing_relocation>', self.bodies['arch_bss_zero'])
        self.assertIn('<data_copy_xip_relocation>', self.bodies['arch_data_copy'])
        preflight = self.bodies['crux_flash_island_prepare']
        self.assertIn('<crux_island_relocations_valid>', preflight)
        self.assertNotIn('<crux_island_enter>', preflight)
        self.assertRegex(self.bodies['main'], r'<(?:__)?crux_observation_run(?:_veneer)?>')
        if '__crux_observation_run_veneer' in self.bodies:
            self.assertIn(hex(self.symbols['crux_observation_run'])[2:], self.bodies['__crux_observation_run_veneer'])

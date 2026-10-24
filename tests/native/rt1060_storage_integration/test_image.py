"""Exact ARM consumer ABI, initialization and newly retained read-result evidence."""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[3]
@unittest.skipUnless(os.environ.get('PILOT_ELF'), 'requires final ARM candidate')
class Image(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.elf=Path(os.environ['PILOT_ELF'])
        spec=importlib.util.spec_from_file_location('image', ROOT/'tests/native/flash_target_candidate/assess_image.py')
        cls.image=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.image)
        cls.symbols=cls.image.symbols(cls.elf.read_bytes())
        cls.disassembly=subprocess.check_output([os.environ['ARM_OBJDUMP'],'-d',str(cls.elf)],text=True)
        cls.bodies={m[1]:m[2] for m in re.finditer(r'^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',cls.disassembly,re.M|re.S)}
    def test_independent_c17_cpp17_consumers_match_arm_abi_without_native_headers(self):
        commands=json.loads((self.elf.parents[1]/'compile_commands.json').read_text())
        selected=[x for x in commands if re.search(r'candidate_(?:file|config)_consumer_',x['command'])]
        self.assertEqual(len(selected),4)
        for item in selected:
            command=item['command']
            self.assertIn('-mcpu=cortex-m7',command);self.assertIn('-mthumb',command)
            self.assertIn('-mabi=aapcs',command)
            self.assertNotRegex(command,r'-m(?:32|64)\b|-fno-p(?:ic|ie)\b')
            self.assertNotIn('__ZEPHYR__',command);self.assertNotIn('autoconf.h',command)
            self.assertNotIn('/zephyr/include',command)
            self.assertIn('-std=c++17' if item['file'].endswith('.cpp') else '-std=c17',command)
    def test_main_inert_deferred_devices_and_private_trial_retained(self):
        main=self.bodies['main']
        self.assertNotRegex(main,r'\b(?:bl|blx|str)\b')
        for name in ['crux_candidate_fixed_trial','crux_candidate_read','crux_candidate_write',
                     'crux_candidate_erase','crux_candidate_command_prerequisites']:
            self.assertIn(name,self.symbols)
        gate=self.bodies['crux_candidate_command_prerequisites']
        self.assertNotRegex(gate,r'\b(?:bl|blx|str|ldr)\b')
        self.assertRegex(gate,r'mvn\.w\s+r0, #12')
        dts=(self.elf.parent/'zephyr.dts').read_text()
        self.assertGreaterEqual(dts.count('zephyr,deferred-init;'),5)
    def test_new_ram_result_function_and_command_state(self):
        self.assertLess(self.symbols['crux_rom_command_read_result'],0x20000)
        state=self.symbols['crux_rom_command']
        self.assertTrue(0x20000000<=state<0x20020000)
        body=self.bodies['crux_rom_command_read_result']
        self.assertNotRegex(body,r'\b(?:bl|blx)\b')
        self.assertNotRegex(body,r'\.word\s+0x60[0-7][0-9a-f]{5}')
        self.assertRegex(body,r'\bldrb\b');self.assertRegex(body,r'\bstrb\b')
if __name__=='__main__':unittest.main()

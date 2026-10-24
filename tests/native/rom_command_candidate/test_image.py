"""Final ARM closure for the retained, unexecuted target command candidate."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[3]
@unittest.skipUnless(os.environ.get('PILOT_ELF'),'requires exact final ARM image')
class CommandImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('image',ROOT/'tests/native/flash_target_candidate/assess_image.py')
        cls.image=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.image)
        cls.elf=Path(os.environ['PILOT_ELF']).read_bytes();cls.symbols=cls.image.symbols(cls.elf)
        cls.disassembly=subprocess.check_output([os.environ['ARM_OBJDUMP'],'-d',os.environ['PILOT_ELF']],text=True)
        cls.bodies={m[1]:m[2] for m in re.finditer(r'^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',cls.disassembly,re.M|re.S)}
    def test_command_state_and_consumed_metadata_are_internal_ram(self):
        for name in ['crux_rom_command','crux_rom_nor','crux_rom_memc_data','crux_rom_memc_config']:
            self.assertIn(name,self.symbols)
            self.assertGreaterEqual(self.symbols[name],0x20000000)
            self.assertLess(self.symbols[name],0x20020000)
        self.assertLess(self.symbols['crux_rom_command_prepare'],0x20000)
    def test_dispatch_call_and_literal_closure(self):
        pending=['crux_rom_command_dispatch'];seen=set()
        forbidden=['set_device_config','update_clock','probe','sfdp','UpdateLUT','SetFlashConfig','SystemReset']
        while pending:
            name=pending.pop()
            if name in seen:continue
            seen.add(name)
            self.assertTrue(name in self.bodies,"missing function body: "+name)
            self.assertLess(self.symbols[name]&~1,0x20000,name)
            self.assertFalse(any(token in name for token in forbidden),name)
            body=self.bodies[name]
            self.assertNotRegex(body,r'\bv(?:push|pop|ldr|str|mov|add|sub|mul|div)')
            self.assertNotRegex(body,r'\bblx\s+r\d+|\bbx\s+r(?:[0-9]|1[0-2])\b|\b(?:ldr|mov)\s+pc,')
            addresses=[int(x,16) for x in re.findall(r'^\s*([0-9a-f]+):',body,re.M)]
            for address,target in re.findall(r'\b(?:bl|b(?:\.[nw])?)\s+([0-9a-f]+)\s+<([^>]+)>',body):
                address=int(address,16)
                if min(addresses)<=address<=max(addresses):
                    continue  # objdump may label internal offsets with absolute CONFIG symbols.
                self.assertLess(address,0x20000,(name,target))
                matches=[key for key,value in self.bodies.items()
                         if int(re.search(r'^\s*([0-9a-f]+):',value,re.M)[1],16)==address]
                self.assertEqual(len(matches),1,(name,target))
                pending.append(matches[0])
            for value in re.findall(r'\.word\s+0x([0-9a-f]+)',body):
                self.assertFalse(0x60000000<=int(value,16)<0x60800000,(name,value))
        # Conservative static bound: sum every reachable function frame, even
        # mutually exclusive paths. No recursion or dynamic stack allocation.
        frames={}
        for name in seen:
            body=self.bodies[name]
            frame=sum(4*len(regs.split(',')) for regs in re.findall(
                r'\b(?:push|stmdb)\s+(?:sp!,\s*)?\{([^}]+)\}',body))
            frame+=sum(map(int,re.findall(r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*#(\d+)',body)))
            self.assertNotRegex(body,r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*r\d+')
            frames[name]=frame
        self.assertLessEqual(sum(frames.values()),1024)
        if os.environ.get('COMMAND_CLOSURE_JSON'):
            import json
            Path(os.environ['COMMAND_CLOSURE_JSON']).write_text(json.dumps({
                'functions':{name:{'address':self.symbols[name], 'frame_upper_bytes':frames[name]}
                             for name in sorted(seen)},
                'all_frames_conservative_sum':sum(frames.values()),
                'operation_stack_bytes':1024,
                'scope':'static software call/literal/stack closure; no hardware execution'},indent=2)+'\n')
        self.assertIn('memc_flexspi_transfer',seen)
        self.assertIn('FLEXSPI_TransferBlocking',seen)
        self.assertIn('FLEXSPI_SoftwareReset',seen)
        self.assertIn('crux_flash_poc_terminal',seen)
        self.assertIn('crux_rom_elapsed_expired',seen)
    def test_context_calls_dispatch_after_ram_setup_and_terminals_on_error(self):
        body=self.bodies['crux_island_enter']
        calls=re.findall(r'\bbl\s+[^<]*<([^>]+)>',body)
        self.assertEqual(calls,['crux_rom_command_dispatch'])
        self.assertLess(body.lower().index('msr\tpsp'),body.index('<crux_rom_command_dispatch>'))
        after=body[body.index('<crux_rom_command_dispatch>'):]
        self.assertRegex(after,r'cmp\s+r0, #0[\s\S]*bne[^\n]*<crux_island_exception_terminal>')
        self.assertRegex(after,r'mov\s+ip, sl[\s\S]*msr\s+PRIMASK, r0[\s\S]*mov\s+r0, ip')
    def test_target_gate_has_no_command_or_timer_calls(self):
        gate=self.bodies['crux_rom_command_submit']
        self.assertNotRegex(gate,r'\b(?:bl|blx|str|ldr)\b')
        self.assertRegex(gate,r'mvn\.w\s+r0, #12')
        elapsed=self.bodies['crux_rom_elapsed_expired']
        self.assertNotRegex(elapsed,r'\b(?:bl|blx|str|ldr)\b')
        self.assertRegex(elapsed,r'movs\s+r0, #1')

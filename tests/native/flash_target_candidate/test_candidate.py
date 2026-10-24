"""Actual pinned function bodies with controlled effects; no hardware access."""
import errno
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

PREPARE = module('candidate_prepare', HERE / 'prepare.py')
ACCEPTED = module('accepted_tests', ROOT / 'tests/native/flash_safety/test_driver.py')
ACCEPTED.PREPARE = PREPARE
class InheritedLowerChecks(unittest.TestCase):
    test_actual_lower_waits_terminate = ACCEPTED.NativeErrors.test_actual_lower_waits_terminate
    test_hal_progressing_loops_not_budgeted = ACCEPTED.NativeErrors.test_hal_progressing_loops_not_budgeted
    test_wrong_source_rejected = ACCEPTED.NativeErrors.test_wrong_source_rejected
ZEPHYR = Path(os.environ['ZEPHYR_BASE'])

# Each exact extracted composition compiles once; cases execute fresh processes.
BUILDS = {}

def execute(code, args):
    if code not in BUILDS:
        tmp = tempfile.TemporaryDirectory()
        source = Path(tmp.name) / 'case.c'
        exe = Path(tmp.name) / 'case'
        source.write_text(code)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-sign-compare',
                        '-I' + str(ROOT / 'tests/native/storage_target_guard'),
                        str(source), '-o', str(exe)], check=True)
        BUILDS[code] = (tmp, exe)
    return subprocess.run([str(BUILDS[code][1]), *map(str, args)], capture_output=True,
                          text=True, timeout=3, check=True).stdout.strip()


class StartupErrors(unittest.TestCase):
    def test_probe_failure_cannot_reset_unlock_or_continue(self):
        text = PREPARE.transform_nor((ZEPHYR / 'drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        code = (HERE / 'startup_stubs.h').read_text()
        code += ACCEPTED.definition(text, 'flash_flexspi_nor_probe')
        code += '''
int main(int argc, char **argv) {
 if(argc!=2)return 2;
 stage=atoi(argv[1]);struct flash_flexspi_nor_data data={0};
 if(!setjmp(terminal)) {result=flash_flexspi_nor_probe(&data);}
 printf("%d %d %d %d %d\\n",reason,resets,unlocks,sfdp_calls,result);
 return 0;
}
'''
        # Faults cover initial clock, initial config, JEDEC transport, SFDP,
        # oversized parameters, second SFDP, mode config, final config.
        for stage in range(1, 9):
            with self.subTest(stage=stage):
                reason, resets, unlocks, sfdp, result = map(int, execute(code, [stage]).split())
                self.assertEqual(reason, -errno.ENOBUFS if stage == 5 else -errno.EIO)
                self.assertEqual((resets, unlocks, result), (0, 0, 777))
                if stage == 3:
                    self.assertEqual(sfdp, 0)
        self.assertEqual(execute(code, [0]), '0 1 1 0 0')
        self.assertEqual(execute(code, [9]), '0 1 1 2 0')

class InitErrors(unittest.TestCase):
    def test_postprobe_clock_error_cannot_return(self):
        text = PREPARE.transform_nor((ZEPHYR / 'drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        code = (HERE / 'startup_stubs.h').read_text()
        code += """
#define DT_ANY_INST_HAS_PROP_STATUS_OKAY(x) 0
#define LOG_ERR(...) ((void)0)
struct flash_flexspi_nor_config {const struct device *controller;};
static bool device_is_ready(const struct device *d) {(void)d;return true;}
static int flash_flexspi_nor_probe(struct flash_flexspi_nor_data *d) {(void)d;return stage==10?-EIO:0;}
"""
        code += ACCEPTED.definition(text, 'flash_flexspi_nor_init')
        code += """
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 stage=atoi(argv[1]);(void)flexspi_probe_lut;(void)flash_flexspi_nor_base_lut;struct device controller={0};
 struct flash_flexspi_nor_data data={0};
 struct flash_flexspi_nor_config config={.controller=&controller};
 struct device dev={.data=&data,.config=&config};
 if(!setjmp(terminal))result=flash_flexspi_nor_init(&dev);
 printf("%d %d %d\\n",reason,resets,result);return 0;
}
"""
        self.assertEqual(execute(code, [1]), f'{-errno.EIO} 0 777')
        self.assertEqual(execute(code, [10]), f'{-errno.EIO} 0 777')
        self.assertEqual(execute(code, [0]), '0 1 0')

class LowerWaits(unittest.TestCase):
    def test_actual_fifo_and_command_wait_dispositions(self):
        path = ZEPHYR.parent / 'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c'
        text = PREPARE.transform_hal(path.read_text())
        code = (HERE / 'hal_stubs.h').read_text()
        code += '\n'.join(ACCEPTED.definition(text, name) for name in
                          ['FLEXSPI_WriteBlocking', 'FLEXSPI_ReadBlocking', 'FLEXSPI_TransferBlocking'])
        code += """
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 mode=atoi(argv[1]);FLEXSPI_Type regs={0};uint32_t buffer[4]={0};int ret=777;
 flexspi_transfer_t transfer={.SeqNumber=1};
 regs.intr[1]=mode==6||mode==9?1:mode==7?2:mode==8?4:0;
 if(!setjmp(terminal)) {
  if(mode==0||mode==6||mode==9)ret=FLEXSPI_WriteBlocking(&regs,(uint8_t *)buffer,8);
  else if(mode==1||mode==2||mode==7||mode==10)ret=FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,mode==2?1:8);
  else ret=FLEXSPI_TransferBlocking(&regs,&transfer);
 }
 printf("%d %d %u %u\\n",reason,ret,accesses,idles);return 0;
}
"""
        for mode in range(11):
            with self.subTest(mode=mode):
                reason, result, accesses, idles = map(int, execute(code, [mode]).split())
                if mode <= 5:
                    self.assertEqual((reason, result), (-errno.ETIMEDOUT, 777))
                    self.assertEqual(accesses + idles, [1025, 1025, 2049, 1027, 1025, 1028][mode])
                else:
                    self.assertEqual((reason, result), (0, -1 if mode >= 9 else 0))

class RuntimeFences(unittest.TestCase):
    def runtime_code(self):
        text = PREPARE.transform_nor((ZEPHYR / 'drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        code = (ROOT / 'tests/native/flash_safety/native_stubs.h').read_text()
        code = code.replace('static unsigned irq_lock(void) {', 'static unsigned irq_lock(void) { calls[5]++;')
        code = code.replace('(void)d;\n    (void)p;\n    (void)o;\n    return (void *)1;', '(void)d; (void)p; (void)o; calls[4]++; return (void *)1;')
        code += '\n#include <string.h>\n#include "request_fence.h"\n'
        code += '#define CONFIG_FLASH_MCUX_FLEXSPI_NOR_WRITE_BUFFER 1\n'
        code += 'static unsigned char nor_write_buf[256];\n'
        code += '\n'.join(ACCEPTED.definition(text, name) for name in
                          ['area_is_subregion', 'flash_flexspi_nor_wait_bus_busy',
                           'flash_flexspi_nor_write', 'flash_flexspi_nor_erase'])
        return code + """
int main(int argc,char **argv) {
 if(argc!=6&&argc!=7)return 2;
 if(argc==7)fail_after=atoi(argv[6]);
 int op=atoi(argv[1]);off_t offset=strtoll(argv[2],NULL,0);size_t length=strtoull(argv[3],NULL,0);
 fail_stage=atoi(argv[4]);xip=atoi(argv[5]);
 struct flash_flexspi_nor_data data={.size=8388608,.legacy_poll=true,.config={.flashSize=8192}};
 struct device dev={.data=&data};uint8_t buffer[768]={0};int ret=777;
 if(!setjmp(terminal))ret=op?flash_flexspi_nor_erase(&dev,offset,length):flash_flexspi_nor_write(&dev,offset,buffer,length);
 printf("%d %d %d %d %d %d %d %d %d\\n",ret,terminal_reason,unlocks,calls[1],calls[2],calls[3],resets,calls[5],calls[4]);
 return 0;
}
"""

    def test_native_rejection_precedes_effects(self):
        code = self.runtime_code()
        requests = [(0, 0, 1), (0, -1, 1), (0, 0x61ffff, 2),
                    (0, 0x7effff, 2), (0, 0x800000, 1), (0, 0x620000, 0),
                    (1, 0, 0x800000), (1, 0x620001, 4096),
                    (1, 0x7ef000, 8192), (1, 0x620000, 4095)]
        for request in requests:
            with self.subTest(request=request):
                result = execute(code, [*request, 0, 1])
                self.assertEqual(result, f'{-errno.EACCES} 0 0 0 0 0 0 0 0')

    def test_actual_runtime_error_stops_later_commands(self):
        code = self.runtime_code()
        for xip in [0, 1]:
            for op, size in [(0, 256), (0, 768), (1, 4096), (1, 65536), (1, 131072)]:
                for failure in range(5):
                    with self.subTest(xip=xip, op=op, size=size, failure=failure):
                        result, term, unlocks, wren, command, polls, resets, locks, ahb = map(
                            int, execute(code, [op, 0x620000, size, failure, xip]).split())
                        self.assertEqual((locks, ahb), (xip, 1))
                        error = -errno.ETIMEDOUT if failure == 4 else -errno.EIO if failure else 0
                        if failure:
                            self.assertEqual(term, error if xip else 0)
                            self.assertEqual(result, 777 if xip else error)
                            self.assertEqual((unlocks, resets, wren), (0, 0, 1))
                            self.assertEqual(command, 0 if failure == 1 else 1)
                        else:
                            self.assertEqual((term, result, unlocks), (0, 0, xip))
                            self.assertEqual(wren, 3 if op == 0 and size == 768 else
                                             2 if size == 131072 else 1)

    def test_later_operation_failure_never_continues_or_unlocks(self):
        code = self.runtime_code()
        for xip in [0, 1]:
            for op, size in [(0, 768), (1, 196608)]:
                for failure in [1, 2, 3]:
                    with self.subTest(xip=xip, op=op, failure=failure):
                        result, term, unlocks, wren, command, polls, resets, locks, ahb = map(
                            int, execute(code, [op, 0x620000, size, failure, xip, 2]).split())
                        self.assertEqual((result, term), (777, -errno.EIO) if xip else (-errno.EIO, 0))
                        self.assertEqual((unlocks, wren, resets, locks, ahb), (0, 2, 1, xip, 1))
                        self.assertEqual(command, 1 if failure == 1 else 2)
                        self.assertEqual(polls, 2 if failure == 3 else 1)

class GeneratorPublication(unittest.TestCase):
    def test_generated_sources_and_manifest_do_not_follow_existing_links(self):
        for link in ['symlink', 'hardlink']:
            with self.subTest(link=link), tempfile.TemporaryDirectory() as tmp:
                workspace = Path(tmp) / 'workspace'
                paths = ['zephyr/drivers/flash/flash_mcux_flexspi_nor.c',
                         'zephyr/drivers/memc/memc_mcux_flexspi.c',
                         'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c']
                output = Path(tmp) / 'generated'
                output.mkdir()
                victims = []
                for relative in paths:
                    source = workspace / relative
                    source.parent.mkdir(parents=True, exist_ok=True)
                    source.write_text('pinned fixture')
                    victims.append(source)
                    destination = output / source.name
                    if link == 'symlink':
                        destination.symlink_to(source)
                    else:
                        os.link(source, destination)
                manifest_victim = workspace / 'manifest-victim'
                manifest_victim.write_text('retained manifest victim')
                victims.append(manifest_victim)
                if link == 'symlink':
                    (output / 'source-hashes.json').symlink_to(manifest_victim)
                else:
                    os.link(manifest_victim, output / 'source-hashes.json')
                before = [victim.read_bytes() for victim in victims]
                with patch.multiple(PREPARE, transform_nor=lambda source: source + ' candidate',
                                    transform_memc=lambda source: source + ' candidate',
                                    transform_hal=lambda source: source + ' candidate'):
                    PREPARE.prepare(workspace, output)
                self.assertEqual([victim.read_bytes() for victim in victims], before)
                for relative in paths:
                    destination = output / Path(relative).name
                    self.assertFalse(destination.is_symlink())
                    self.assertEqual(destination.read_text(), 'pinned fixture candidate')
                self.assertFalse((output / 'source-hashes.json').is_symlink())

class ActualStartupHelpers(unittest.TestCase):
    def helper_code(self, name):
        import re
        text = PREPARE.transform_nor((ZEPHYR / 'drivers/flash/flash_mcux_flexspi_nor.c').read_text())
        text = text.replace('static int\nflash_', 'static int flash_')
        body = ACCEPTED.definition(text, name)
        code = (HERE / 'helper_stubs.h').read_text()
        # Pure LUT instruction constants: controlled values consume source operands.
        for token in sorted(set(re.findall(r'\b(?:kFLEXSPI_\w+|SPI_NOR_\w+)\b', body))):
            code += f'\n#define {token} 0\n'
        if name in ['flash_flexspi_nor_4byte_enable', 'flash_flexspi_nor_is25_clear_dummy_cycles']:
            code = code.replace(ACCEPTED.definition(code, name), '')
        return code + body

    def test_configuration_errors_are_not_reported_as_success(self):
        code = self.helper_code('flash_flexspi_nor_config_flash') + """
int main(int argc,char **argv) {
 if(argc!=2)return 2;mode=atoi(argv[1]);
 struct flash_flexspi_nor_data data={0};struct jesd216_sfdp_header h={0};
 struct jesd216_bfp b={0};uint32_t lut[16][4]={{0}};
 int ret=flash_flexspi_nor_config_flash(&data,&h,&b,lut);
 printf("%d %d\\n",ret,quad_calls);return 0;
}
"""
        for mode, expected in [(1, f'{-errno.EIO} 0'), (3, f'{-errno.EIO} 1'),
                               (4, f'{-errno.ENOTSUP} 1'), (2, '0 1'), (0, '0 1')]:
            with self.subTest(mode=mode):
                self.assertEqual(execute(code, [mode]), expected)

    def test_is25_failed_read_does_not_write_parameters(self):
        code = self.helper_code('flash_flexspi_nor_is25_clear_dummy_cycles') + """
int main(void) {
 mode=5;struct flash_flexspi_nor_data data={0};uint32_t lut[16][4]={{0}};
 int ret=flash_flexspi_nor_is25_clear_dummy_cycles(&data,lut);
 printf("%d %d %d\\n",ret,transfers,configs);return 0;
}
"""
        self.assertEqual(execute(code, []), f'{-errno.EIO} 1 1')

    def test_fourbyte_failed_wren_does_not_configure_or_issue_mode_command(self):
        code = self.helper_code('flash_flexspi_nor_4byte_enable') + """
int main(void) {
 mode=6;struct flash_flexspi_nor_data data={0};uint32_t lut[16][4]={{0}};
 int ret=flash_flexspi_nor_4byte_enable(&data,lut,BIT(1));
 printf("%d %d %d %d\\n",ret,wren_calls,transfers,configs);return 0;
}
"""
        self.assertEqual(execute(code, []), f'{-errno.EIO} 1 0 0')

    def test_jedec_octal_error_and_unsupported_semantics(self):
        code = self.helper_code('flash_flexspi_nor_check_jedec') + """
int main(int argc,char **argv) {
 if(argc!=2)return 2;mode=atoi(argv[1]);struct flash_flexspi_nor_data data={0};
 uint32_t lut[16][4]={{0}};int ret=flash_flexspi_nor_check_jedec(&data,lut);
 printf("%d %d\\n",ret,octal_calls);return 0;
}
"""
        for mode, expected in [(7, f'{-errno.EIO} 1'), (8, '0 1'),
                               (9, f'{-errno.EIO} 0'), (10, f'{-errno.ENOTSUP} 0')]:
            with self.subTest(mode=mode):
                self.assertEqual(execute(code, [mode]), expected)

if __name__ == '__main__':
    unittest.main()

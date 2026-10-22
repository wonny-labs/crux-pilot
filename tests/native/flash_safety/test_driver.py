"""Actual pinned function bodies with controlled effects, not hardware evidence."""
import errno
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location('prepare',ROOT/'scripts/prepare-flash-poc.py')
PREPARE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(PREPARE)
if not os.environ.get('ZEPHYR_BASE'):
    raise SystemExit('Set ZEPHYR_BASE to the pinned west workspace Zephyr directory')
ZEPHYR=Path(os.environ['ZEPHYR_BASE'])
def definition(text,name):
    m=re.search(r'^(?:static )?(?:ALWAYS_INLINE )?(?:int|bool|void|status_t) '+name+r'\(',text,re.M)
    start=text.index('{',m.start());end=start+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]
class NativeErrors(unittest.TestCase):
 def test_error_propagation_and_uncertain_xip_terminal(self):
  text=PREPARE.transform_nor((ZEPHYR/'drivers/flash/flash_mcux_flexspi_nor.c').read_text())
  code=Path(__file__).with_name('native_stubs.h').read_text()
  code+='\n#include <string.h>\n#define CONFIG_FLASH_MCUX_FLEXSPI_NOR_WRITE_BUFFER 1\n#define CONFIG_HAS_MCUX_CACHE 1\nstatic unsigned char nor_write_buf[256];\nstatic void DCACHE_InvalidateByRange(uintptr_t addr,size_t size) {(void)addr;(void)size;}\n'
  code+='\n'.join(definition(text,n) for n in ['area_is_subregion','flash_flexspi_nor_wait_bus_busy','flash_flexspi_nor_write','flash_flexspi_nor_erase'])
  code+='''
int main(int argc,char **argv) {
 if(argc!=5)return 2;
 int multiple=atoi(argv[4]);fail_after=multiple==2?16:multiple?2:1;
 int op=atoi(argv[1]); fail_stage=atoi(argv[2]); xip=atoi(argv[3]);
 struct flash_flexspi_nor_data d={.size=8388608,.legacy_poll=true,.config={.flashSize=8192}};
 struct device dev={.data=&d}; uint8_t buf[768]={0}; int ret=0;
 if(!setjmp(terminal)) {
  ret=op==0?flash_flexspi_nor_write(&dev,0,buf,multiple?768:256):
   flash_flexspi_nor_erase(&dev,op==1||op==5?4096:0,op==5?139264:op==1||op==4?4096:op==2?(multiple?196608:65536):8388608);
 }
 printf("%d %d %d %d %d %d %d\\n",ret,terminal_reason,unlocks,calls[1],calls[2],calls[3],resets);return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
   subprocess.run([os.environ.get('CC','cc'),'-std=c17','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
   for xip in [0,1]:
    for op in range(5):
     for fail in range(5):
      with self.subTest(xip=xip,operation=op,failure=fail):
       run=subprocess.run([str(exe),str(op),str(fail),str(xip),"0"],capture_output=True,text=True,check=True,timeout=2)
       ret,term,unlocks,wren,command,polls,resets=map(int,run.stdout.split())
       expected=-errno.ETIMEDOUT if fail==4 else -errno.EIO if fail else 0
       self.assertEqual(term,expected if xip and fail else 0)
       self.assertEqual(ret,0 if xip and fail else expected)
       self.assertEqual(unlocks,1 if xip and not fail else 0)
       self.assertEqual(resets,0 if fail else 1)
       self.assertEqual(wren,1)
       self.assertEqual(command,0 if fail==1 else 1)
       if fail in [1,2]:self.assertEqual(polls,0)
   for xip in [0,1]:
    for op in [0,2]:
     for fail in [1,2,3]:
      with self.subTest(xip=xip,operation=op,later_failure=fail):
       run=subprocess.run([str(exe),str(op),str(fail),str(xip),"1"],capture_output=True,text=True,check=True,timeout=2)
       ret,term,unlocks,wren,command,polls,resets=map(int,run.stdout.split())
       self.assertEqual(term,-errno.EIO if xip else 0)
       self.assertEqual(ret,0 if xip else -errno.EIO)
       self.assertEqual(unlocks,0)
       self.assertEqual(resets,1)
       self.assertEqual(wren,2)
       self.assertEqual(command,1 if fail==1 else 2)
       self.assertEqual(polls,2 if fail==3 else 1)
   for xip in [0,1]:
    for fail in [1,2,3]:
     with self.subTest(xip=xip,mixed_erase_failure=fail):
      run=subprocess.run([str(exe),"5",str(fail),str(xip),"2"],capture_output=True,text=True,check=True,timeout=2)
      ret,term,unlocks,wren,command,polls,resets=map(int,run.stdout.split())
      self.assertEqual(term,-errno.EIO if xip else 0)
      self.assertEqual(ret,0 if xip else -errno.EIO)
      self.assertEqual(unlocks,0)
      self.assertEqual(resets,15)
      self.assertEqual(wren,16)
      self.assertEqual(command,15 if fail==1 else 16)
      self.assertEqual(polls,16 if fail==3 else 15)
 def test_actual_lower_waits_terminate(self):
  memc=PREPARE.transform_memc((ZEPHYR/'drivers/memc/memc_mcux_flexspi.c').read_text())
  hal=PREPARE.transform_hal((ZEPHYR.parent/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text())
  code="""
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
typedef struct {volatile uint32_t MCR0;} FLEXSPI_Type;
#define FLEXSPI_MCR0_SWRESET_MASK 1U
static FLEXSPI_Type regs;
struct device {int unused;};
static jmp_buf terminal;
static int mode,reason;static unsigned observations;
static bool FLEXSPI_GetBusIdleStatus(FLEXSPI_Type *base) {(void)base;observations++;return mode==2;}
static FLEXSPI_Type *get_base(const struct device *dev) {(void)dev;return &regs;}
static _Noreturn void crux_flash_poc_terminal(int error) {reason=error;longjmp(terminal,1);}
"""
  code+=definition(memc,'memc_flexspi_wait_bus_idle')+definition(hal,'FLEXSPI_SoftwareReset')
  code+="""
int main(int argc,char **argv) {
 if (argc != 2) {
  return 2;
 }
 mode = atoi(argv[1]);
 struct device dev = {0};
 if(!setjmp(terminal)) {if(mode==0)memc_flexspi_wait_bus_idle(&dev);else FLEXSPI_SoftwareReset(&regs);return 3;}
 return reason==-ETIMEDOUT && observations<=1025 ? 0:4;
}
"""
  with tempfile.TemporaryDirectory() as tmp:
   c=Path(tmp)/'lower.c';exe=Path(tmp)/'lower';c.write_text(code)
   subprocess.run([os.environ.get('CC','cc'),'-std=c17','-Wall','-Wextra','-Werror','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
   for mode in range(3):
    with self.subTest(lower_wait=mode):
     try:run=subprocess.run([str(exe),str(mode)],timeout=2)
     except subprocess.TimeoutExpired:self.fail('native lower wait did not reach bounded terminal disposition')
     self.assertEqual(run.returncode,0)
 def test_hal_progressing_loops_not_budgeted(self):
  text=PREPARE.transform_hal((ZEPHYR.parent/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text())
  # Data-size draining loops are progress loops, not hardware wait observations.
  self.assertEqual(len(re.findall(r'while \(0U != size\)',text)),2)
 def test_wrong_source_rejected(self):
  with self.assertRaises(ValueError):PREPARE.transform_nor('unsupported')
if __name__=='__main__':unittest.main()

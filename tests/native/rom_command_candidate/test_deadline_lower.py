"""Actual selected staged wait bodies consume actual candidate timer policy."""
import importlib.util
import os
from pathlib import Path
import unittest
import test_deadline as timer
from test_deadline import ROOT, HERE

class LowerDeadline(unittest.TestCase):
    def test_selected_published_waits_use_candidate(self):
        spec=importlib.util.spec_from_file_location('commands',ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
        import tempfile
        ws=Path(os.environ['ZEPHYR_BASE']).parent
        with tempfile.TemporaryDirectory() as tmp:
            stage.BASE.prepare(ws,Path(tmp));stage.prepare(ws,Path(tmp))
            for name in ['memc_mcux_flexspi.c','fsl_flexspi.c']:
                self.assertIn('crux_rom_candidate_wait_check()', (Path(tmp)/name).read_text(),name)

    def test_actual_wait_faults_terminal_with_real_timer(self):
        spec=importlib.util.spec_from_file_location('commands',ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
        self.assertTrue(hasattr(stage,'deadline_waits'), 'missing selected lower deadline stage')
        spec=importlib.util.spec_from_file_location('accepted',ROOT/'tests/native/flash_safety/test_driver.py')
        accepted=importlib.util.module_from_spec(spec);spec.loader.exec_module(accepted)
        ws=Path(os.environ['ZEPHYR_BASE']).parent
        hal=stage.deadline_waits(stage.BASE.transform_hal((ws/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text()),False)
        memc=stage.deadline_waits(stage.BASE.transform_memc((ws/'zephyr/drivers/memc/memc_mcux_flexspi.c').read_text()),True)
        stubs=(ROOT/'tests/native/flash_target_candidate/hal_stubs.h').read_text().split('static jmp_buf terminal;')[0]
        stubs=stubs.replace('static int mode, reason;', 'static int mode;')
        stubs+='''
#define FLEXSPI_MCR0_SWRESET_MASK 1
struct device { FLEXSPI_Type *base; };
static FLEXSPI_Type *get_base(const struct device *d) {return d->base;}
'''
        stubs=stubs.replace('volatile uint32_t IPTXFCR,','volatile uint32_t MCR0, IPTXFCR,')
        extra=stubs+'\n'+'\n'.join(accepted.definition(hal,name) for name in
            ['FLEXSPI_WriteBlocking','FLEXSPI_ReadBlocking','FLEXSPI_TransferBlocking','FLEXSPI_SoftwareReset'])
        extra+='\n'+accepted.definition(memc,'memc_flexspi_wait_bus_idle')
        setup=(HERE/'deadline_cases.c').read_text().split('int main(')[0]
        cases=setup+r'''
int main(int argc,char **argv) {
 assert(argc==2);(void)argv;
 for(unsigned wait=0;wait<12;wait++)for(unsigned fault=0;fault<7;fault++) {
  initialized();mode=0;accesses=0;idles=0;terminal_reason=0;
  assert(crux_rom_candidate_deadline_begin(fault==0?3:100000)==0);
  if(fault==1)timer_mode=3;
  if(fault==2)gpt.cr[0]^=GPT_CR_EN_MASK;
  if(fault==3)ccm.CCGR0=0;
  if(fault==4)xtal.LOWPWR_CTRL^=1;
  if(fault==5)gpt.PR=1;
  if(fault==6)gpt.CNT-=2;
  FLEXSPI_Type regs={0};struct device d={&regs};uint32_t buffer[4]={0};
  flexspi_transfer_t transfer={.SeqNumber=1};
  if(!setjmp(terminal)) {
   switch(wait) {
   case 0:mode=4;memc_flexspi_wait_bus_idle(&d);break;
   case 1:FLEXSPI_WriteBlocking(&regs,(uint8_t *)buffer,8);break;
   case 2:FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,8);break;
   case 3:FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,1);break;
   case 4:mode=4;FLEXSPI_TransferBlocking(&regs,&transfer);break;
   case 5:mode=3;FLEXSPI_TransferBlocking(&regs,&transfer);break;
   case 6:mode=5;regs.intr[1]=4;FLEXSPI_TransferBlocking(&regs,&transfer);break;
   case 7:FLEXSPI_SoftwareReset(&regs);break;
   case 8:memc_flexspi_wait_bus_idle(&d);break;
   case 9:mode=6;regs.intr[1]=1;FLEXSPI_WriteBlocking(&regs,(uint8_t *)buffer,8);break;
   case 10:mode=7;regs.intr[1]=2;FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,8);break;
   case 11:mode=4;FLEXSPI_SoftwareReset(&regs);break;
   }
   fprintf(stderr,"unsafe return wait=%u fault=%u\n",wait,fault);assert(0); /* No lower uncertainty can return to caller. */
  }
  assert(terminal_reason==-ETIMEDOUT);assert(crux_rom_deadline.fault);
  if(fault>=2)assert(!regs.IPCMD&&!regs.MCR0&&!idles); /* faults before command/reset */
 }
 for(unsigned healthy=0;healthy<6;healthy++) {
  initialized();assert(crux_rom_candidate_deadline_begin(1000)==0);
  mode=0;accesses=0;idles=0;terminal_reason=0;
  FLEXSPI_Type regs={0};struct device d={&regs};uint32_t buffer[4]={0};
  flexspi_transfer_t transfer={.SeqNumber=1};
  if(setjmp(terminal))assert(0);
  switch(healthy) {
  case 0:memc_flexspi_wait_bus_idle(&d);break;
  case 1:mode=6;regs.intr[1]=1;assert(FLEXSPI_WriteBlocking(&regs,(uint8_t *)buffer,8)==0);break;
  case 2:mode=7;regs.intr[1]=2;assert(FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,8)==0);break;
  case 3:mode=6;assert(FLEXSPI_TransferBlocking(&regs,&transfer)==0);break;
  case 4:mode=9;regs.intr[1]=1;assert(FLEXSPI_WriteBlocking(&regs,(uint8_t *)buffer,8)==-1);break;
  case 5:mode=10;assert(FLEXSPI_ReadBlocking(&regs,(uint8_t *)buffer,8)==-1);break;
  }
  assert(terminal_reason==0&&!crux_rom_deadline.fault);
 }
 fresh();terminal_reason=0;
 if(!setjmp(terminal)){crux_rom_candidate_wait_check();assert(0);}
 assert(terminal_reason==-ETIMEDOUT);
 return 0;
}
'''
        timer.Deadline().run_case(0,extra,cases)

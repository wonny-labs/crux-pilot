"""Newly selected actual SDK LUT wait consumes actual GPT2 candidate policy."""
import importlib.util
import os
from pathlib import Path
import unittest
import test_deadline as timer
from test_deadline import ROOT,HERE
class IdentityDeadline(unittest.TestCase):
    def test_actual_lut_wait_and_ready_boundaries_consume_timer(self):
        spec=importlib.util.spec_from_file_location('stage',ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
        spec=importlib.util.spec_from_file_location('accepted',ROOT/'tests/native/flash_safety/test_driver.py')
        accepted=importlib.util.module_from_spec(spec);spec.loader.exec_module(accepted)
        ws=Path(os.environ['ZEPHYR_BASE']).parent
        hal=stage.deadline_waits(stage.BASE.transform_hal((ws/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text()),False)
        body=accepted.definition(hal,'FLEXSPI_UpdateLUT')
        extra=r'''
#include <assert.h>
#define FLEXSPI_LUT_COUNT 64
#define FLEXSPI_LUT_KEY_VAL 0x5af05af0U
typedef struct {uint32_t LUT[64], LUTKEY,LUTCR;} FLEXSPI_Type;
static bool busy;
static bool FLEXSPI_GetBusIdleStatus(FLEXSPI_Type *base){(void)base;return !busy;}
static bool FLEXSPI_CheckInputLutLocation(FLEXSPI_Type *base,uint32_t p){(void)base;(void)p;return true;}
'''+body
        setup=(HERE/'deadline_cases.c').read_text().split('int main(')[0]
        cases=setup+r'''
int main(int argc,char **argv) {
 (void)argc;(void)argv;
 for(unsigned state=0;state<2;state++)for(unsigned fault=0;fault<7;fault++) {
  initialized();assert(crux_rom_candidate_deadline_begin(fault==0?3:100000)==0);
  busy=state;terminal_reason=0;
  if(fault==1)timer_mode=3;
  if(fault==2)gpt.cr[0]^=GPT_CR_EN_MASK;
  if(fault==3)ccm.CCGR0=0;
  if(fault==4)xtal.LOWPWR_CTRL^=1;
  if(fault==5)gpt.PR=1;
  if(fault==6)gpt.CNT-=2;
  FLEXSPI_Type regs={0};uint32_t source[4]={0x2401049f,0,0,0};
  if(!setjmp(terminal)) {FLEXSPI_UpdateLUT(&regs,60,source,4);assert(0);}
  assert(terminal_reason==-ETIMEDOUT&&crux_rom_deadline.fault);
  if(fault>=1)assert(!regs.LUTKEY&&!regs.LUT[60]&&!regs.LUTCR);
 }
 initialized();assert(crux_rom_candidate_deadline_begin(1000)==0);
 busy=false;FLEXSPI_Type regs={0};uint32_t source[4]={0x2401049f,0,0,0};
 FLEXSPI_UpdateLUT(&regs,60,source,4);
 assert(regs.LUT[60]==source[0]&&regs.LUTKEY==FLEXSPI_LUT_KEY_VAL&&regs.LUTCR==1);
 return 0;
}
'''
        timer.Deadline().run_case(0,extra,cases)

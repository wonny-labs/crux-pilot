"""Native MEMC consumed bookkeeping and transfer, using controlled SDK leaf."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
class Metadata(unittest.TestCase):
    def test_exact_binding_and_native_a1_transfer(self):
        spec=importlib.util.spec_from_file_location('accepted',ROOT/'tests/native/flash_safety/test_driver.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        text=(Path(os.environ['ZEPHYR_BASE'])/'drivers/memc/memc_mcux_flexspi.c').read_text()
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "rom_command.h"
#define BUILD_ASSERT(x) _Static_assert(x, "direct MMIO required")
#define CONFIG_MMU 0
#define IS_ENABLED(x) (x)
#define LOG_ERR(...) ((void)0)
#define MEMC_FLEXSPI_CMD_PER_SEQ 4
#define DEVICE_MMIO_NAMED_GET(d,n) (((const struct memc_flexspi_config *)(d)->config)->n.addr)
enum {kFLEXSPI_PortA1,kFLEXSPI_PortA2,kFLEXSPI_PortB1,kFLEXSPI_PortB2,kFLEXSPI_PortCount};
struct device {const void *config;void *data;};
struct mmio {uintptr_t addr;};
struct memc_flexspi_config {struct mmio reg_base,ahb;};
struct memc_flexspi_data {size_t size[4];struct {uint8_t lut_offset,lut_used;} port_luts[4];};
typedef struct {unsigned deviceAddress,port,seqIndex,SeqNumber,dataSize,cmdType;uint32_t *data;} flexspi_transfer_t;
typedef unsigned FLEXSPI_Type;
typedef int status_t;
enum {kStatus_Success};
static int fail,calls;
static status_t FLEXSPI_TransferBlocking(FLEXSPI_Type *base,flexspi_transfer_t *t) {
 assert((uintptr_t)base==0x402a8000);assert(t->port==0);assert(t->seqIndex==9);
 assert(t->deviceAddress==0x620000);calls++;return fail;
}
static inline FLEXSPI_Type *get_base(const struct device *dev) {
 return (FLEXSPI_Type *)DEVICE_MMIO_NAMED_GET(dev,reg_base);
}
'''
        code+=(ROOT/'samples/flash_target_candidate/rom_command_memc.inc').read_text()
        code+=module.definition(text,'memc_flexspi_transfer')
        code+=r'''
int main(void) {
 struct device d={0};assert(crux_rom_memc_bind(&d)==0);
 assert(crux_rom_memc_data.size[0]==8388608);
 assert(crux_rom_memc_data.port_luts[0].lut_offset==0);
 flexspi_transfer_t t={.port=0,.seqIndex=9,.deviceAddress=0x620000};
 assert(memc_flexspi_transfer(&d,&t)==0);assert(calls==1);
 fail=1;assert(memc_flexspi_transfer(&d,&t)==-EIO);assert(calls==2);
 crux_rom_memc_data.size[1]=1;assert(crux_rom_memc_bind(&d)==-EACCES);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'case.c';exe=Path(tmp)/'case';source.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-Wno-sign-compare',
                '-I'+str(ROOT/'samples/flash_target_candidate'),
                '-I'+str(ROOT/'tests/native/storage_target_guard'),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_target_gate_refuses_without_selecting_time(self):
        code=(ROOT/'samples/flash_target_candidate/rom_command_gate.c').read_text()+r'''
#include <assert.h>
int main(void) {
 struct crux_rom_request request={CRUX_ROM_PROGRAM,CRUX_PILOT_FLASH_FILE,0x620000,4,"test"};
 assert(crux_rom_command_submit(&request)==-EACCES);
 assert(crux_rom_command_submit(NULL)==-EACCES);
 assert(crux_rom_elapsed_expired());return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'gate.c';exe=Path(tmp)/'gate';source.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'samples/flash_target_candidate'),
                '-I'+str(ROOT/'tests/native/storage_target_guard'),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_final_stage_manifest_matches_published_sources(self):
        spec=importlib.util.spec_from_file_location('stage',ROOT/'tests/native/flash_target_candidate/stage_commands.py')
        stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
        import hashlib
        import json
        workspace=Path(os.environ['ZEPHYR_BASE']).parent
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)
            stage.BASE.prepare(workspace,output)
            original=json.loads((output/'source-hashes.json').read_text())
            stage.prepare(workspace,output)
            final=json.loads((output/'source-hashes.json').read_text())
            for name in ['flash_mcux_flexspi_nor.c','memc_mcux_flexspi.c']:
                self.assertEqual(final[name]['candidate_sha256'],hashlib.sha256((output/name).read_bytes()).hexdigest())
                self.assertEqual(final[name]['base_candidate_sha256'],original[name]['candidate_sha256'])

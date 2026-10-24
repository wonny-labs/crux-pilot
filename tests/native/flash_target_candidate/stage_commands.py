"""Sibling command stage; preserve accepted/candidate source transforms."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SPEC=importlib.util.spec_from_file_location('candidate_base',HERE/'prepare.py')
BASE=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(BASE)

def selected_helpers(source):
    # Validate actual pinned input through the existing accepted transform.
    BASE.BASE.transform_nor(source)
    result=[]
    for suffix,index in [('read_status',1),('write_enable',3),('erase_sector',5),('page_program',9)]:
        name='flash_flexspi_nor_'+suffix
        start,end=BASE.function_span(source,name)
        body=source[start:end].replace(name,'crux_rom_native_'+suffix,1)
        body,count=re.subn(r'(\.seqIndex\s*=\s*)\w+',lambda m:m[1]+str(index),body)
        if count!=1: raise ValueError('selected helper sequence anchor changed')
        result.append(body)
    return '\n'.join(result)+'\n'

def deadline_waits(source, memc):
    """Only selected busy graph; accepted/base generators remain unchanged."""
    names = ['memc_flexspi_wait_bus_idle'] if memc else [
        'FLEXSPI_SoftwareReset', 'FLEXSPI_WriteBlocking',
        'FLEXSPI_ReadBlocking', 'FLEXSPI_TransferBlocking']
    for name in names:
        match = re.search(r'^\w[^\n;]*\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
        if not match:
            raise ValueError('selected deadline function changed: ' + name)
        start = source.index('{', match.start())
        end = start + 1
        depth = 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        body = source[start:end]
        body, count = re.subn(r'for \(unsigned int poc_budget = 1024U; (.*?); \)',
            r'for (unsigned int poc_budget = 1024U; (crux_rom_candidate_wait_check(), \1); )', body)
        expected = {'memc_flexspi_wait_bus_idle': 1, 'FLEXSPI_SoftwareReset': 2,
                    'FLEXSPI_WriteBlocking': 1, 'FLEXSPI_ReadBlocking': 2,
                    'FLEXSPI_TransferBlocking': 3}[name]
        if count != expected:
            raise ValueError('selected deadline observation count changed: ' + name)
        body = body[:1] + '\n    crux_rom_candidate_wait_check();' + body[1:]
        body = re.sub(r'\breturn\b', 'crux_rom_candidate_wait_check(); return', body)
        body = body[:-1] + '\n    crux_rom_candidate_wait_check();\n}'
        source = source[:start] + body + source[end:]
    return '#include "rom_deadline.h"\n' + source

def prepare(workspace,output):
    workspace=Path(workspace).resolve(); output=Path(output).resolve()
    if output.is_relative_to(workspace/'zephyr') or output.is_relative_to(workspace/'modules/hal/nxp'):
        raise ValueError('output must be outside upstream')
    nor=(workspace/'zephyr/drivers/flash/flash_mcux_flexspi_nor.c').read_text()
    memc=(workspace/'zephyr/drivers/memc/memc_mcux_flexspi.c').read_text()
    base_nor=BASE.transform_nor(nor)
    base_memc=BASE.transform_memc(memc)
    base_hal=BASE.transform_hal((workspace/'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c').read_text())
    provenance=json.loads((output/'source-hashes.json').read_text())
    generated={
      'flash_mcux_flexspi_nor.c':base_nor+'\n'+selected_helpers(nor)+
            '\n#include "rom_command_nor.inc"\n',
      'memc_mcux_flexspi.c':deadline_waits(base_memc,True)+'\n#include "rom_command_memc.inc"\n',
      'fsl_flexspi.c':deadline_waits(base_hal,False),
    }
    metadata={}
    for name,text in generated.items():
        BASE.publish(output,name,text)
        metadata[name]=hashlib.sha256(text.encode()).hexdigest()
        base={'flash_mcux_flexspi_nor.c':base_nor, 'memc_mcux_flexspi.c':base_memc, 'fsl_flexspi.c':base_hal}[name]
        provenance[name]['base_candidate_sha256']=hashlib.sha256(base.encode()).hexdigest()
        provenance[name]['candidate_sha256']=metadata[name]
    BASE.publish(output,'source-hashes.json',json.dumps(provenance,indent=2)+'\n')
    BASE.publish(output,'command-source-hashes.json',json.dumps(metadata,indent=2)+'\n')
if __name__=='__main__':
    import sys
    prepare(sys.argv[1],sys.argv[2])

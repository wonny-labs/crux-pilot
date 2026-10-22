"""Check selected PoC ELF placements; not complete XIP closure or flash permission."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def placements(data):
    if data[:6]!=b'\x7fELF\x01\x01':raise ValueError('expected ELF32 little endian')
    h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    if h[2]!=40 or h[11]!=40:raise ValueError('expected ARM section layout')
    sections=[struct.unpack_from('<IIIIIIIIII',data,h[6]+i*h[11]) for i in range(h[12])]
    symbols={}
    for section in sections:
        if section[1]!=2:continue
        strings_section=sections[section[6]]
        strings=data[strings_section[4]:strings_section[4]+strings_section[5]]
        for offset in range(section[4],section[4]+section[5],16):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,offset)
            symbols[strings[name:].split(b'\0',1)[0].decode()]=value
    required=['crux_flash_poc_terminal','flash_flexspi_nor_write','flash_flexspi_nor_erase',
              'memc_flexspi_transfer','FLEXSPI_TransferBlocking','FLEXSPI_SoftwareReset']
    for name in required:
        if name not in symbols or not 0<=symbols[name]<0x20000:
            raise ValueError(name+' is not in ITCM')
    config=symbols.get('memc_flexspi_config_0',0)
    if not 0x80000000<=config<0x82000000:raise ValueError('MEMC configuration is not in RAM')
    return {name:hex(symbols[name]) for name in required+['memc_flexspi_config_0']}
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    args=parser.parse_args();data=args.elf.read_bytes()
    print(json.dumps({'elf_sha256':hashlib.sha256(data).hexdigest(),'selected_placements':placements(data),
                      'limitation':'selected symbols only; no NMI/vector/data/call-graph or hardware safety claim'},indent=2))

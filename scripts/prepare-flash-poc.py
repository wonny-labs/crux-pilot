"""Pinned-source experimental transform; upstream trees are read-only inputs."""
import hashlib
from pathlib import Path
import re
NOR_HASH='397019890b4ce7afae88c7da1913ab0b8ac57c3ee1a2c1975c7ba6c4c08a3ea4'
def transform_nor(source):
    if hashlib.sha256(source.encode()).hexdigest()!=NOR_HASH:
        raise ValueError('unsupported NOR source')
    def bounds(name):
        match=re.search(r"^static int "+name+r"\(",source,re.M)
        start=source.index("{",match.start());end=start+1;depth=1
        while depth:
            depth+=(source[end]=="{")-(source[end]=="}");end+=1
        return match.start(),end
    replacements=[]
    for name in ['flash_flexspi_nor_write','flash_flexspi_nor_erase']:
        start,end=bounds(name);body=source[start:end]
        brace=body.index('{')+1
        body=body[:brace]+"\n\tint ret = 0;"+body[brace:]
        body=re.sub(r"(?m)^(\s*)(flash_flexspi_nor_(?:write_enable|page_program|erase_sector|erase_block|erase_chip|wait_bus_busy)\([^;]+);$",
                    lambda m:m[1]+"ret = "+m[2]+";\n"+m[1]+"if (ret) { goto poc_exit; }",body)
        exit_at=body.rfind('\n\tif (memc_flexspi_is_running_xip')
        body=body[:exit_at]+"\npoc_exit:\n\tif (ret && memc_flexspi_is_running_xip(&data->controller)) {\n\t\tcrux_flash_poc_terminal(ret);\n\t}\n"+body[exit_at:]
        at=body.rfind('return 0;');body=body[:at]+body[at:].replace('return 0;','return ret;',1)
        replacements.append((start,end,body))
    start,end=bounds('flash_flexspi_nor_wait_bus_busy');body=source[start:end]
    body=body.replace('int ret;','int ret;\n\tunsigned int poc_budget = 1024U;')
    body=body.replace('while (1) {','while (1) {\n\t\tif (poc_budget-- == 0U) { return -ETIMEDOUT; }',1)
    replacements.append((start,end,body))
    for start,end,body in sorted(replacements,reverse=True):
        source=source[:start]+body+source[end:]
    return '#include "flash_poc.h"\n'+source

MEMC_HASH='0b9cfcd4fef488d542977b628dbc0c0c3c9243fff1d50cc8dc7315b49e74ce20'
HAL_HASH='d57aec16e69a553c79c14bfb67bb1d729f50680db92b155f8b6016734e564b4a'

# Experimental bounded observation loops, deliberately independent of IRQ clocks.
def _bounded(source):
    edits=[]
    for m in re.finditer(r'(?m)^\s*while\s*\(',source):
        left=source.index('(',m.start());right=left+1;depth=1
        while depth:
            depth+=(source[right]=='(')-(source[right]==')');right+=1
        if source[left+1:right-1].strip() == '0U != size':
            continue
        brace=right
        while source[brace].isspace():brace+=1
        if source[brace]!='{':raise ValueError('unbraced native loop')
        edits.append((m.start(),brace+1,
            '\n    for (unsigned int poc_budget = 1024U; '+source[left+1:right-1]+'; )\n    {\n'
            '        if (poc_budget-- == 0U) { crux_flash_poc_terminal(-ETIMEDOUT); }'))
    for start,end,replacement in reversed(edits):source=source[:start]+replacement+source[end:]
    return '#include "flash_poc.h"\n'+source

def transform_memc(source):
    if hashlib.sha256(source.encode()).hexdigest()!=MEMC_HASH:raise ValueError('unsupported MEMC source')
    old='static const struct memc_flexspi_config memc_flexspi_config_##n'
    if source.count(old)!=1:raise ValueError('unexpected MEMC config')
    return _bounded(source.replace(old,'static struct memc_flexspi_config memc_flexspi_config_##n'))

def transform_hal(source):
    if hashlib.sha256(source.encode()).hexdigest()!=HAL_HASH:raise ValueError('unsupported HAL source')
    return _bounded(source)

def prepare(workspace, output):
    import json
    workspace=Path(workspace).resolve();output=Path(output).resolve()
    zephyr=workspace/'zephyr';hal=workspace/'modules/hal/nxp'
    if output.is_relative_to(zephyr) or output.is_relative_to(hal):
        raise ValueError('output must be outside pinned upstream trees')
    files=[(zephyr/'drivers/flash/flash_mcux_flexspi_nor.c',transform_nor),
           (zephyr/'drivers/memc/memc_mcux_flexspi.c',transform_memc),
           (hal/'mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c',transform_hal)]
    generated=[(src,fn(src.read_text())) for src,fn in files]
    output.mkdir(parents=True,exist_ok=True)
    metadata={}
    for src,text in generated:
        (output/src.name).write_text(text)
        metadata[src.name]={'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),
                            'candidate_sha256':hashlib.sha256(text.encode()).hexdigest()}
    (output/'source-hashes.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return metadata
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();prepare(args.workspace,args.output)

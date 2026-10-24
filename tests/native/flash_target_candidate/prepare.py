"""Isolated proposal generator; pinned upstream and accepted PoC stay read-only."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('accepted_flash_poc', ROOT / 'scripts/prepare-flash-poc.py')
BASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BASE)
transform_nor = BASE.transform_nor
transform_memc = BASE.transform_memc
transform_hal = BASE.transform_hal


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('unexpected pinned candidate anchor: ' + old[:60])
    return source.replace(old, new, 1)


def function_span(source, name):
    import re
    match = re.search(r'^static int\s+' + name + r'\(', source, re.M)
    start = source.index('{', match.start())
    end = start + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return match.start(), end


def transform_nor(source):
    source = BASE.transform_nor(source)
    start, end = function_span(source, 'flash_flexspi_nor_probe')
    body = source[start:end]
    body = replace_once(body,
        '\tret = flash_flexspi_nor_sfdp_read_helper(data, 0, sfdp_buf, sizeof(sfdp_buf));',
        '\t/* Only unsupported JEDEC devices may fall back to SFDP. */\n'
        '\tif (ret != -ENOTSUP) { goto _exit; }\n\n'
        '\tret = flash_flexspi_nor_sfdp_read_helper(data, 0, sfdp_buf, sizeof(sfdp_buf));')
    body = replace_once(body, '\tif (ret < 0) {\n\t\treturn ret;\n\t}\n\n_exit:',
                        '\tif (ret < 0) {\n\t\tgoto _exit;\n\t}\n\n_exit:')
    body = replace_once(body, '_exit:\n\tmemc_flexspi_reset',
        '_exit:\n\t/* No reset/unlock/return after uncertain probe or mode writes. */\n'
        '\tif (ret && memc_flexspi_is_running_xip(&data->controller)) {\n'
        '\t\tcrux_flash_poc_terminal(ret);\n\t}\n\tmemc_flexspi_reset')
    source = source[:start] + body + source[end:]
    start, end = function_span(source, 'flash_flexspi_nor_init')
    body = source[start:end]
    body = replace_once(body, '\tif (flash_flexspi_nor_probe(data)) {',
        '\tint ret = flash_flexspi_nor_probe(data);\n'
        '\tif (ret) {\n'
        '\t\tif (memc_flexspi_is_running_xip(&data->controller)) {\n'
        '\t\t\tcrux_flash_poc_terminal(ret);\n\t\t}\n')
    body = replace_once(body,
        '\tif (memc_flexspi_update_clock(&data->controller, &data->config,\n'
        '\t\t\t\t\tdata->port, data->config.flexspiRootClk)) {',
        '\tret = memc_flexspi_update_clock(&data->controller, &data->config,\n'
        '\t\t\t\t\tdata->port, data->config.flexspiRootClk);\n'
        '\tif (ret) {\n'
        '\t\tif (memc_flexspi_is_running_xip(&data->controller)) {\n'
        '\t\t\tcrux_flash_poc_terminal(ret);\n\t\t}\n')
    source = source[:start] + body + source[end:]
    # Replace the two native comment-only IS25/init spins with the same audited hook.
    import re
    source, count = re.subn(r'while \(1\) \{\s*/\*.*?\*/\s*\}',
                            'crux_flash_poc_terminal(-EIO);', source, flags=re.S)
    if count != 2:
        raise ValueError('unexpected startup spin count')
    start, end = function_span(source, 'flash_flexspi_nor_is25_clear_dummy_cycles')
    body = source[start:end]
    body = replace_once(body,
        '\tret = memc_flexspi_transfer(&data->controller, &transfer);\n',
        '\tret = memc_flexspi_transfer(&data->controller, &transfer);\n'
        '\tif (ret < 0) { return ret; }\n')
    source = source[:start] + body + source[end:]
    start, end = function_span(source, 'flash_flexspi_nor_4byte_enable')
    body = source[start:end]
    body = replace_once(body, '\t\tflash_flexspi_nor_write_enable(data);',
        '\t\tret = flash_flexspi_nor_write_enable(data);\n'
        '\t\tif (ret < 0) { return ret; }')
    source = source[:start] + body + source[end:]
    start, end = function_span(source, 'flash_flexspi_nor_config_flash')
    body = source[start:end]
    body = replace_once(body, '\t\t\t\t\t\t     dw16.enter_4ba);',
        '\t\t\t\t\t\t     dw16.enter_4ba);\n'
        '\t\tif (ret < 0 && ret != -ENOTSUP) { return ret; }')
    body = replace_once(body, '\t\t\t\t\t\t\t    dw15.qer);',
        '\t\t\t\t\t\t\t    dw15.qer);\n'
        '\t\t\tif (ret < 0) { return ret; }')
    source = source[:start] + body + source[end:]
    start, end = function_span(source, 'flash_flexspi_nor_check_jedec')
    body = source[start:end]
    body = replace_once(body, '\t\tflash_flexspi_nor_octal_enable(data, flexspi_lut);',
        '\t\tret = flash_flexspi_nor_octal_enable(data, flexspi_lut);\n'
        '\t\tif (ret < 0) { return ret; }')
    source = source[:start] + body + source[end:]
    for name, length, erase in [('flash_flexspi_nor_write', 'len', 'false'),
                                ('flash_flexspi_nor_erase', 'size', 'true')]:
        start, end = function_span(source, name)
        body = source[start:end]
        brace = body.index('{') + 1
        body = body[:brace] + ('\n\tif (!crux_pilot_flash_request_valid(offset, ' + length +
               ', ' + erase + ')) { return -EACCES; }\n') + body[brace:]
        source = source[:start] + body + source[end:]
    source = '#include "request_fence.h"\n' + source
    return source


def publish(output, name, text):
    """Publish a fresh inode without following a previous file's links."""
    import os
    import tempfile
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=output,
                                         prefix='.candidate-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text)
        os.replace(temporary, output / name)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def prepare(workspace, output):
    import hashlib
    import json
    workspace = Path(workspace).resolve()
    output = Path(output).resolve()
    zephyr = workspace / 'zephyr'
    hal = workspace / 'modules/hal/nxp'
    if output.is_relative_to(zephyr) or output.is_relative_to(hal):
        raise ValueError('output must be outside pinned upstream trees')
    files = [(zephyr / 'drivers/flash/flash_mcux_flexspi_nor.c', transform_nor),
             (zephyr / 'drivers/memc/memc_mcux_flexspi.c', transform_memc),
             (hal / 'mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c', transform_hal)]
    generated = [(source, transform(source.read_text())) for source, transform in files]
    output.mkdir(parents=True, exist_ok=True)
    metadata = {}
    for source, text in generated:
        publish(output, source.name, text)
        metadata[source.name] = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                                 'candidate_sha256': hashlib.sha256(text.encode()).hexdigest()}
    publish(output, 'source-hashes.json', json.dumps(metadata, indent=2) + '\n')
    return metadata


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.workspace, args.output)

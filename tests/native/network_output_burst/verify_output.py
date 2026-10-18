"""Check exact production-format native burst delivery, not physical performance."""
import argparse
import ast
import json
from pathlib import Path
import re
import shutil
import tempfile

CASES = {'crux.native.network_burst':4096, 'crux.native.network_paced':4096,
         'crux.native.network_burst_8k':8192, 'crux.native.network_burst_16k':16384}
STAMP = 9223372036854625807


def expected_records(header):
    formats = {}
    for line in header.read_text().splitlines():
        if line.startswith('#define M3_FMT_'):
            name, value = line[len('#define M3_FMT_'):].split(' ',1)
            formats[name] = ast.literal_eval(value)
    rows=[]
    def emit(name,*values):
        fmt=re.sub(r'%(?:lld|llu|zu|u)', '%d', formats[name])
        line=fmt % values
        if not line.endswith('\n') or '\n' in line[:-1]:raise ValueError('Unexpected production format ending')
        rows.append(line[:-1])
    def resources():
        emit('RESOURCE',STAMP,0,0,16,16,32,32)
        emit('STATE',STAMP,1,1,0)
    def cleanup():emit('CLEANUP_OK',0,0,5000)
    emit('REQUESTED');resources();emit('ROUND_BUDGET',7,STAMP,STAMP+150000);cleanup()
    for protocol in (1,2):
        for sequence in range(1,11):
            emit('EXCHANGE',7,protocol,sequence,-116,STAMP,2000)
            resources();cleanup();resources()
    emit('ROUND',7,-5,STAMP);resources();emit('STACK',0,4096,1024)
    emit('LOGGER',STAMP,STAMP,1,0,2048,2048,1024,14,0)
    if len(rows)!=130:raise ValueError('Incorrect fixture volume')
    return rows


def verify(root, announce=True):
    logs=[p for p in root.rglob('handler.log') if p.parent.name in CASES]
    found={p.parent.name:p for p in logs}
    if len(logs)!=len(CASES) or set(found)!=set(CASES):raise ValueError('Missing burst comparison handler')
    result={}
    for name,path in found.items():
        config=(path.parent/'zephyr/.config').read_text().splitlines()
        required={'CONFIG_BOARD_NATIVE_SIM=y','CONFIG_LOG_PRINTK=y','CONFIG_LOG_MODE_DEFERRED=y',
                  'CONFIG_LOG_PROCESS_THREAD=y','CONFIG_LOG_BACKEND_UART=y',
                  'CONFIG_MAIN_THREAD_PRIORITY=0','CONFIG_LOG_PROCESS_THREAD_PRIORITY=14',
                  'CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048',f'CONFIG_LOG_BUFFER_SIZE={CASES[name]}'}
        forbidden={'CONFIG_PRINTK_SYNC=y','CONFIG_LOG_BLOCK_IN_THREAD=y','CONFIG_LOG_MODE_OVERFLOW=y',
                   'CONFIG_LOG_MODE_IMMEDIATE=y','CONFIG_LOG_BACKEND_UART_ASYNC=y'}
        allowed={'CONFIG_LOG_BACKEND_UART=y','CONFIG_LOG_BACKEND_UART_AUTOSTART=y',
                 'CONFIG_LOG_BACKEND_UART_OUTPUT_TEXT=y','CONFIG_LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP=y'}
        backends={s for s in config if s.startswith('CONFIG_LOG_BACKEND_') and s.endswith('=y')}
        if not required.issubset(config) or forbidden.intersection(config) or not backends.issubset(allowed):
            raise ValueError('Unreviewed native burst configuration')
        lines=[s[:-1] if s.endswith('\r') else s for s in path.read_bytes().decode('ascii').split('\n')]
        expected=expected_records(path.parent/'production_formats.h')
        actual=[s for s in lines if 'M3_NET_' in s]
        completed=[s for s in lines if 'NETWORK_BURST_COMPLETE' in s]
        if len(completed)!=1:raise ValueError('Missing/duplicate completion')
        metadata=re.fullmatch(r'NETWORK_BURST_COMPLETE emitted=130 capacity=([0-9]+) queued_bytes=([0-9]+) usage_status=0',completed[0])
        if not metadata or int(metadata[1])!=CASES[name]-4 or not 0<=int(metadata[2])<=int(metadata[1]):
            raise ValueError('Incorrect actual pre-drain memory snapshot')
        drops=[]
        for line in lines:
            if 'messages dropped' in line:
                match=re.fullmatch(r'--- ([0-9]+) messages dropped ---',line)
                if not match:raise ValueError('Malformed native loss marker')
                drops.append(int(match[1]))
        lost=sum(drops)
        if len(actual)+lost!=130:raise ValueError('Delivered/dropped count does not conserve finite volume')
        cursor=0
        for line in actual:
            try:cursor=expected.index(line,cursor)+1
            except ValueError as error:raise ValueError('Invalid content, order, duplication or formatting') from error
        lossless=actual==expected and not drops
        if name in ('crux.native.network_paced','crux.native.network_burst_16k') and not lossless:
            raise ValueError('Required lossless comparison did not deliver exact volume')
        if name=='crux.native.network_burst' and (lossless or lost==0):
            raise ValueError('Preserved actual4KiB rapid overflow not reproduced')
        result[name]=dict(status='LOSSLESS_FULL_VOLUME' if lossless else 'LOSS_REPRODUCED',
                          delivered=len(actual),dropped=lost,configured_bytes=CASES[name],usable_bytes=int(metadata[1]),queued_bytes=int(metadata[2]))
    if announce:print(json.dumps(result,indent=2))
    return result


def replay_negative_controls(root):
    source=next(p for p in root.rglob('handler.log') if p.parent.name=='crux.native.network_burst_16k')
    raw=source.read_bytes();record=next(s for s in raw.split(b'\n') if s.startswith(b'M3_NET_EXCHANGE '))
    mutations=(b' '+record,record+b' ',b'\x1b[31m'+record,record+b'\n'+record,
               record[:len(record)//2],record.replace(b'sequence=1 ',b'sequence=2 ',1))
    for changed in mutations:
        with tempfile.TemporaryDirectory() as temporary:
            target=Path(temporary)
            for p in root.rglob('handler.log'):
                if p.parent.name not in CASES:continue
                d=target/p.parent.name;d.mkdir();(d/'zephyr').mkdir()
                for name in ('handler.log','production_formats.h','zephyr/.config'):
                    shutil.copy2(p.parent/name,d/name)
            verify(target,False)
            (target/source.parent.name/'handler.log').write_bytes(raw.replace(record,changed,1))
            try:verify(target,False)
            except ValueError:pass
            else:raise ValueError('Malformed actual-handler replay accepted')
    print('ACTUAL_NETWORK_BURST_REPLAY_NEGATIVES_PASS; no physical performance claim')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    root=parser.parse_args().directory
    verify(root);replay_negative_controls(root)

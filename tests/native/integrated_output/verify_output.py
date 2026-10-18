"""Verify the finite native fixture stream; this is not controller/watchdog evidence."""
import argparse,ast,json,re,shutil,tempfile
from pathlib import Path

CASES=('crux.native.integrated_output','crux.native.integrated_output_loss')


def formats(header):
    return {line[len('#define M3_FMT_'):].split(' ',1)[0]:ast.literal_eval(line.split(' ',2)[2])
            for line in header.read_text().splitlines() if line.startswith('#define M3_FMT_')}


def render(fmt,values):
    return (re.sub(r'%(?:lld|llu|zu|u)', '%d',fmt) % values).removesuffix('\n')


def expected(fmt,loss):
    owners={'main':[],'worker':[],'network':[]}
    def emit(owner,name,*values):owners[owner].append((name,values))
    emit('main','BOOT','0123456789ab',1);emit('main','WDT_ARMED')
    for n in range(500 if loss else 60):
        stamp=10000+n*100
        emit('main','WDT_FEED',stamp,100+n,stamp,n//10+1,stamp-n%10*100)
        if not loss and n%10==0:
            emit('main','OBSERVER_ALIVE',stamp,1,100+n,3,n//10+1,stamp,1,0,70000)
            emit('main','STACK',0,4160,4096,1024,0,4096,1024,0,4160,4096,1024)
            emit('main','LOGGER',stamp)
    if not loss:
        emit('main','FAULT_STALL',16000,160,15900,30900,70000)
        emit('main','NET_FAULT_STALL',16000,6,15010,15900,30900,70000)
        emit('main','BOOT','0123456789ab',0)
        for n in range(6):
            stamp=10000+n*1000
            emit('worker','WORK',100+n*10,stamp,0,n%2,0)
            def resources(at):
                emit('network','NET_RESOURCE',at,0,0,16,16,32,32)
                emit('network','NET_STATE',at,1,1,1)
            resources(stamp)
            for protocol in (1,2):
                emit('network','NET_EXCHANGE',1,protocol,n+1,0,stamp+protocol*5,2)
                resources(stamp+protocol*5);emit('network','NET_CLEANUP',0,0,0);resources(stamp+protocol*5)
            emit('network','NET_WORK',n+1,stamp+10)
    return owners


def matches(line,item,fmt):
    name,values=item
    if name=='LOGGER':
        match=re.fullmatch(r'M3_LOGGER monotonic_ms=(\d+) queue_snapshot_ms=(\d+) found=1 status=0 configured=2048 usable=2048 unused=(\d+) priority=14 queued=(\d+)',line)
        if not match:return False
        stamp,queued_at,unused,queued=map(int,match.groups())
        return stamp==queued_at==values[0] and 0<=unused<=2048 and 0<=queued<=16384 and line==render(
            fmt[name],(stamp,queued_at,1,0,2048,2048,unused,14,queued))
    return line==render(fmt[name],values)


def verify(root,announce=True):
    found={p.parent.name:p for p in root.rglob('handler.log') if p.parent.name in CASES}
    if set(found)!=set(CASES):raise ValueError('Missing actual native integrated fixture case')
    result={}
    for name,path in found.items():
        loss=name.endswith('_loss');config=(path.parent/'zephyr/.config').read_text().splitlines()
        required={'CONFIG_BOARD_NATIVE_SIM=y','CONFIG_LOG_PRINTK=y','CONFIG_LOG_MODE_DEFERRED=y',
            'CONFIG_LOG_PROCESS_THREAD=y','CONFIG_LOG_PROCESS_THREAD_PRIORITY=14',
            'CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048','CONFIG_LOG_BUFFER_SIZE=16384',
            'CONFIG_MAIN_THREAD_PRIORITY=0','CONFIG_MAIN_STACK_SIZE=4096',
            'CONFIG_THREAD_MONITOR=y','CONFIG_THREAD_NAME=y','CONFIG_LOG_BACKEND_UART=y'}
        forbidden={'CONFIG_PRINTK_SYNC=y','CONFIG_LOG_BLOCK_IN_THREAD=y','CONFIG_LOG_MODE_OVERFLOW=y',
            'CONFIG_LOG_MODE_IMMEDIATE=y','CONFIG_LOG_BACKEND_UART_ASYNC=y'}
        allowed={'CONFIG_LOG_BACKEND_UART=y','CONFIG_LOG_BACKEND_UART_AUTOSTART=y',
            'CONFIG_LOG_BACKEND_UART_OUTPUT_TEXT=y','CONFIG_LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP=y'}
        backends={s for s in config if s.startswith('CONFIG_LOG_BACKEND_') and s.endswith('=y')}
        if not required.issubset(config) or forbidden.intersection(config) or not backends.issubset(allowed):
            raise ValueError('Unreviewed integrated fixture configuration')
        lines=[s.removesuffix('\r') for s in path.read_bytes().decode('ascii').split('\n')]
        fmt=formats(path.parent/'production_formats.h');wanted=expected(fmt,loss)
        actual={owner:[] for owner in wanted}
        for line in lines:
            if 'M3_' not in line:continue
            owner=('network' if line.startswith('M3_NET_') and not line.startswith('M3_NET_FAULT_STALL ')
                   else 'worker' if line.startswith('M3_WORK ') else 'main')
            actual[owner].append(line)
        if not loss:
            payload=[line for line in lines if 'M3_' in line]
            arm=payload.index(render(fmt['WDT_ARMED'],()))
            fault=payload.index(render(fmt['FAULT_STALL'],(16000,160,15900,30900,70000)))
            if not payload[0].startswith('M3_BOOT ') or not payload[-1].startswith('M3_BOOT ') or any(
                    not arm < i < fault for i,line in enumerate(payload)
                    if line in actual['worker'] or line in actual['network']):
                raise ValueError('Producer work crossed explicit fixture phase boundaries')
        dropped=0
        for line in lines:
            if 'messages dropped' in line:
                match=re.fullmatch(r'--- (\d+) messages dropped ---',line)
                if not match:raise ValueError('Malformed native loss marker')
                dropped+=int(match[1])
        total=sum(map(len,wanted.values()));delivered=sum(map(len,actual.values()))
        if lines.count(f'INTEGRATED_FIXTURE_COMPLETE emitted={total} expected_loss={int(loss)}')!=1:
            raise ValueError('Missing/incorrect finite completion')
        for owner,items in wanted.items():
            cursor=0
            for line in actual[owner]:
                while cursor<len(items) and not matches(line,items[cursor],fmt):
                    if not loss:raise ValueError('Invalid content, per-producer sequence or logger resource')
                    cursor+=1
                if cursor==len(items):raise ValueError('Unexpected/duplicate/malformed fixture output')
                cursor+=1
            if not loss and cursor!=len(items):raise ValueError('Missing producer records')
        if delivered+dropped!=total or (loss and dropped==0) or (not loss and dropped):
            raise ValueError('Native loss/conservation criterion failed')
        result[name]=dict(status='LOSS_REPRODUCED' if loss else 'EXACT_PER_PRODUCER_DELIVERY',
                         delivered=delivered,dropped=dropped,expected=total)
    if announce:print(json.dumps(result,indent=2))
    return result


def negative_controls(root):
    source=next(p for p in root.rglob('handler.log') if p.parent.name==CASES[0]);raw=source.read_bytes()
    record=next(s for s in raw.split(b'\n') if s.startswith(b'M3_WDT_FEED '))
    mutations=(b' '+record,record+b' ',b'\x1b[0m'+record,record+b'\n'+record,
               record[:len(record)//2],record.replace(b'sequence=100 ',b'sequence=101 ',1))
    for replacement in mutations:
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)
            for path in root.rglob('handler.log'):
                if path.parent.name not in CASES:continue
                folder=target/path.parent.name;(folder/'zephyr').mkdir(parents=True)
                for name in ('handler.log','production_formats.h','zephyr/.config'):shutil.copy2(path.parent/name,folder/name)
            verify(target,False);(target/source.parent.name/'handler.log').write_bytes(raw.replace(record,replacement,1))
            try:verify(target,False)
            except ValueError:pass
            else:raise ValueError('Corrupt actual native stream accepted')
    with tempfile.TemporaryDirectory() as d:
        target=Path(d)
        for path in root.rglob('handler.log'):
            if path.parent.name not in CASES:continue
            folder=target/path.parent.name;(folder/'zephyr').mkdir(parents=True)
            for name in ('handler.log','production_formats.h','zephyr/.config'):shutil.copy2(path.parent/name,folder/name)
        moved=next(s for s in raw.split(b'\n') if s.startswith(b'M3_NET_WORK '))
        changed=raw.replace(moved+b'\n',b'',1).replace(b'INTEGRATED_FIXTURE_COMPLETE',moved+b'\nINTEGRATED_FIXTURE_COMPLETE',1)
        (target/source.parent.name/'handler.log').write_bytes(changed)
        try:verify(target,False)
        except ValueError:pass
        else:raise ValueError('Cross-phase producer output accepted')
    print('ACTUAL_INTEGRATED_OUTPUT_NEGATIVES_PASS; no physical watchdog claim')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);root=parser.parse_args().directory
    verify(root);negative_controls(root)

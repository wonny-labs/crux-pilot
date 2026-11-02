"""M5 bounded capture, operation plans and exact retained-evidence replay.

Preparation/replay commands are inert. The capture subcommand requires explicit
--execute-hardware and exact per-operation approval before opening an accepted
M3 UART lease. It never programs, restores, or switches a cable. A fresh reset requires both
--perform-approved-reset and its individually approved operation.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import m3_uart
import m5_network_oracle as oracle
import m5_series

PROBE='602003106'
UART_USB='02290000325182c300000000000000000000000097969905'
BANNER_PREFIX=b'*** Booting Zephyr OS build'
PREFIX_LIMIT=4096
NIC=dict(interface='en5',model='AX88179',mac='00:0a:cd:41:89:cc',host='192.168.77.1',target='192.168.77.2',prefix=24)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def digest(value):
    oracle.require(isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None,'Require exact SHA-256')
    return value


def command(nonce,sequence,count,ticket,profile=m5_series.FULL):
    oracle.require(type(nonce) is int and 0<nonce<=0xffffffff and type(ticket) is int and 0<ticket<=0xffffffff,'Invalid nonce/readiness ticket')
    expected=next((c for s,c,_ in profile['phases'] if s==sequence),None)
    oracle.require(expected is not None and count==expected,'Command outside fixed phase matrix')
    action='I' if not count else 'L' if sequence==profile['loss'] else 'P'
    fields=[action,str(nonce),str(sequence)]
    if action=='P':fields.append(str(count))
    return ' '.join(fields+[str(ticket)])+'\n'


def plan(manifest_sha256,nonces,entry_manifest_sha256=None,recovery_manifest_sha256=None):
    digest(manifest_sha256)
    oracle.require(len(nonces)==3 and len(set(nonces))==3 and all(type(n) is int and 0<n<=0xffffffff for n in nonces),'Three distinct nonzero uint32 boot nonces required')
    for value in (entry_manifest_sha256,recovery_manifest_sha256):
        if value is not None:digest(value)
    operations=['establish_usb_mount_guard','acquire_exclusive_probe_uart_and_nic','verify_entry_image','program_and_start']
    operations += ['qualification_preflight_'+str(n) for n in range(1,4)]
    operations += ['qualification_reset_'+str(n) for n in range(1,4)]
    for n in range(1,4):operations+=['boot_'+str(n)+'_cable_disconnect','boot_'+str(n)+'_cable_reconnect']
    operations+=['restore_identified_normal_image','verify_normal_image','normal_acceptance_capture']
    boots=[]
    for index,nonce in enumerate(nonces,1):
        commands=[]
        for sequence,count,mode in m5_series.PHASES:
            action='I' if count==0 else 'L' if sequence==7 else 'P'
            template=' '.join([action,str(nonce),str(sequence)]+([str(count)] if action=='P' else [])+['<fresh READY ticket>'])
            commands.append(dict(sequence=sequence,count=count,peer_mode=mode,template=template,peer_command=None if not count else ['python3','tests/target/m5_network_peer.py','--token',str(nonce),'--phase',str(sequence),'--count',str(count),'--mode',mode,'--output','boot-'+str(index)+'-phase-'+str(sequence)+'-peer.jsonl']))
        boots.append(dict(nonce=nonce,reset_operation='qualification_reset_'+str(index),arm_command='A '+str(nonce)+'\n',commands=commands))
    return dict(schema=1,status='SOFTWARE_PREPARED_HARDWARE_NOT_AUTHORIZED',manifest_sha256=manifest_sha256,entry_manifest_sha256=entry_manifest_sha256,recovery_manifest_sha256=recovery_manifest_sha256,runner_sha256=sha(__file__),adapter_sha256=sha(Path(__file__).with_name('m5_capture_adapter.py')),probe_serial=PROBE,uart_usb_serial=UART_USB,nic=dict(NIC),nonces=list(nonces),operations=operations,boots=boots,
        procedure=[
            'Before any device operation: operator verifies written approval of exact operation, manifest, runner, entry and normal recovery image. Read approved M3 procedure; retain pinned J-Link executable/DLL hashes, boot UUID, exclusive owner PID and fresh root USB mount guard. Unknown state: STOP with no automatic reset or retry.',
            'Use accepted M3 exact UART identity, ownership/inventory, isolated NIC identity/static route, no writable storage and QSPI geometry. Revalidate mount guard and exclusive ownership immediately before each device operation. No unapproved firmware maintenance/update.',
            'Retain exact raw UART bytes, receive.jsonl and poll.jsonl from continuous reader. Verify known entry image with accepted full verifybin procedure before programming. Program exact M5 BIN with accepted loadfile/verifybin, then r/g; programming implicit resets/start are covered by program_and_start approval. Retain this prefix separately; it is not a qualifying boot.',
            'For each approved qualification reset, capture distinct M5_BOOT source banner, send exactly one A nonce line; read matching ARM and READY. Before every phase start its exact M5 peer and retain ready record, consume that READY ticket once. Send command immediately; never queue commands, repeat old tickets, send commands after error, or allow another process to write UART.',
            'For phase L7: wait for target BEGIN TCP AND peer request with exact nonce/phase/transaction, record cable_disconnect_cue with shared host CLOCK_MONOTONIC, obtain separately approved physical disconnect and record receipt plus dedicated NIC DOWN. Target DOWN must fall within target entered..functional interval; the cue must precede END receipt; operator/carrier reports may arrive later and cannot replace target in-call DOWN proof. Otherwise INCONCLUSIVE. UDP is attempted while already DOWN; no claim of UDP in-flight loss.',
            'After L7 DONE and READY8: separately approved reconnect; record cable_connect_receipt, dedicated host_up, exact restored route and target primary LINK UP before phase8 BEGIN. Phase8 must remain in same source/nonce/boot. A reset requiring approval is a distinct nonqualifying recovery outcome, never resetless PASS.',
            'Monitor target per-stream clocks, BEGIN/END receipt liveness10s, command150s and series1800s absolute caps; operator wait90s clamped to remaining caps. Functional budget2000ms, whole-call observation above2500ms REVIEW_REQUIRED. Any liveness expiry is ABORT/INCONCLUSIVE; target may still execute; no cancellation/reset is implied.',
            'On completion or functional failure: perform only the separately approved identified normal image restore/verify and accepted normal baseline capture while safety state remains known. On unknown transport, ownership or evidence state stop; restoration/reset/retry is not automatic. Retain all operation and safety evidence for independent review.',
        ],evidence=dict(series='schema1 source/clock/boots; each boot reset(operation,monotonic), nonce, uart_directory, writes_file, controls_file, peer_stops_file and peer_files keyed by phase; relative retained raw artifact paths',raw='Per boot: uart-rx.bin + receive.jsonl + poll.jsonl, peer JSONL per phase; operator controls JSONL; safety/program/verify/restore logs retained separately',control_events=['cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route']))


def approval_template(prepared):
    names=('manifest_sha256','entry_manifest_sha256','recovery_manifest_sha256','runner_sha256','adapter_sha256','probe_serial','uart_usb_serial','nic','nonces')
    return dict(schema=1,authorization='NOT_AUTHORIZED',plan_sha256=hashlib.sha256(json.dumps(prepared,sort_keys=True,separators=(',',':')).encode()).hexdigest(),**{name:copy.deepcopy(prepared[name]) for name in names},operations={name:False for name in prepared['operations']})


def validate_approval(approval,prepared):
    if set(prepared)=={'plan','approval_template'}:prepared=prepared['plan']
    try:canonical=plan(prepared['manifest_sha256'],prepared['nonces'],prepared['entry_manifest_sha256'],prepared['recovery_manifest_sha256'])
    except (KeyError,TypeError) as error:raise ValueError('Malformed preparation plan') from error
    oracle.require(prepared==canonical,'Preparation differs from complete canonical safety plan')
    expected=approval_template(canonical)
    oracle.require(set(approval)==set(expected),'Missing/extra approval fields')
    oracle.require(approval['authorization']=='APPROVED_FOR_PHYSICAL_EXECUTION','Physical execution not authorized')
    for key,value in expected.items():
        if key not in ('authorization','operations'):oracle.require(approval[key]==value,'Approval binding mismatch: '+key)
    for key in ('manifest_sha256','entry_manifest_sha256','recovery_manifest_sha256','runner_sha256','adapter_sha256','plan_sha256'):digest(approval[key])
    oracle.require(set(approval['operations'])==set(expected['operations']) and all(value is True for value in approval['operations'].values()),'Every physical operation requires separate explicit approval')
    return dict(status='APPROVAL_DOCUMENT_COMPLETE_NO_OPERATIONS_EXECUTED',fresh_safety_and_owner_checks='REQUIRED_BEFORE_EACH_OPERATION')


def passive_prefix_line(line):
    if line=='' or re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*',line) or re.fullmatch(r'M5_BOOT source=[0-9a-f]{12}',line) or line=='M5_FAIL stopped=1 no_more_traffic=1':return
    if line.startswith(('M5 LINK ','M5 READY ')):
        oracle.parse_record(line);return
    raise ValueError('Unknown/active pre-reset UART output; no reset permitted')


def qualified_uart_lines(directory,rows):
    """Preserve exact passive prefix and verify explicit approved-reset cut."""
    directory=Path(directory);marker=directory/'reset-boundary.json'
    if not marker.exists():return rows
    boundary=json.loads(marker.read_text());raw=(directory/'uart-rx.bin').read_bytes();offset=0;cut=None
    oracle.require(0<=boundary['raw_offset']<=len(raw),'Invalid reset raw boundary')
    chunks=raw.splitlines(keepends=True)
    oracle.require(len(chunks)==len(rows),'Raw UART line offsets differ')
    for index,((stamp,line),chunk) in enumerate(zip(rows,chunks)):
        if offset>=boundary['raw_offset'] and stamp>=boundary['monotonic'] and re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*',line):cut=(index,offset);break
        passive_prefix_line(line);offset+=len(chunk)
    oracle.require(cut is not None,'No fresh boot after approved reset boundary')
    prefix=json.loads((directory/'prefix-boundary.json').read_text())
    oracle.require(prefix==dict(raw_start=0,raw_end=cut[1],sha256=hashlib.sha256(raw[:cut[1]]).hexdigest(),reason='passive UART before approved fresh reset boot'),'Excluded raw prefix range/hash differs')
    return rows[cut[0]:]


def prefix_record(head):
    return dict(raw_start=0,raw_end=len(head),sha256=hashlib.sha256(head).hexdigest(),reason='pre-banner bytes retained, not parsed')


def focused_rows(directory):
    """Cut retained pre-banner bytes (any junk, possibly sharing the banner's line) before the first banner."""
    directory=Path(directory);raw=(directory/'uart-rx.bin').read_bytes()
    at=raw.find(BANNER_PREFIX);oracle.require(at>=0,'No Zephyr banner in retained UART bytes')
    head=raw[:at]
    oracle.require(len(head)<=PREFIX_LIMIT and b'M5' not in head and json.loads((directory/'focused-prefix.json').read_text())==prefix_record(head),'Retained pre-banner prefix differs or looks like target output')
    # Rebuild lines from the raw bytes after the cut, so pre-banner junk is never decoded.
    events=[json.loads(line) for line in (directory/'receive.jsonl').read_text().splitlines()]
    chunks=[bytes.fromhex(e['hex']) for e in events];stamps=[e['monotonic'] for e in events]
    oracle.require(all(type(v) in (int,float) and math.isfinite(v) for v in stamps) and all(a<=b for a,b in zip(stamps,stamps[1:])),'UART event clock invalid')
    oracle.require(b''.join(chunks)==raw and raw.endswith(b'\n'),'Incomplete or mismatched exact UART event stream')
    ends=[];total=0
    for chunk in chunks:
        total+=len(chunk);ends.append(total)
    rows=[];position=at
    for line in raw[at:].split(b'\n')[:-1]:
        position+=len(line)+1
        stamp=stamps[next(i for i,end in enumerate(ends) if end>=position)]
        rows.append((stamp,(line[:-1] if line.endswith(b'\r') else line).decode('ascii')))
    return rows


def uart_records(directory,focused=False):
    result=[]
    rows=focused_rows(directory) if focused else qualified_uart_lines(directory,m3_uart.exact_event_rows(directory))
    for monotonic,line in rows:
        if line.startswith('M5_BOOT '):
            match=re.fullmatch(r'M5_BOOT source=([0-9a-f]{12})',line)
            oracle.require(match is not None,'Malformed M5 source banner')
            row=dict(event='BOOT',source=match[1])
        elif line.startswith('M5 '):row=oracle.parse_record(line)
        elif line.startswith('M5'):raise ValueError('Target reported failure/unknown M5 record')
        else:
            # Only pinned Zephyr boot boilerplate may precede M5_BOOT. No text
            # correction, status line filtering, or ignored output after boot.
            oracle.require(not result and (line=='' or re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*',line)),'Unexpected UART output')
            continue
        result.append(dict(monotonic=monotonic,record=row))
    oracle.require(result and result[0]['record']['event']=='BOOT','Missing exact M5 boot banner')
    return result


def replay_bundle(index_path,source,abi,profile=m5_series.FULL):
    """Load acceptance input from retained raw files, never embedded UART rows.

    Index schema1 contains source/clock and three boots with nonce, reset,
    uart_directory, writes_file, controls_file, peer_stops_file and peer_files keyed by phase.
    Paths are relative to the index directory; missing/incomplete data fail shut.
    """
    index_path=Path(index_path);base=index_path.resolve().parent
    index=json.loads(index_path.read_text());boots=[]
    def path(name):
        candidate=(base/name).resolve()
        oracle.require(candidate.is_relative_to(base),'Evidence path escapes bundle')
        return candidate
    def jsonl(name):
        return [json.loads(line) for line in path(name).read_text().splitlines()]
    for boot in index['boots']:
        directory=path(boot['uart_directory']);uart=uart_records(directory,profile['name']=='focused')
        polls=[json.loads(line) for line in (directory/'poll.jsonl').read_text().splitlines()]
        stamps=m5_series.times(polls)
        oracle.require(stamps and stamps[0]<=uart[0]['monotonic'] and stamps[-1]>=uart[-1]['monotonic'],'Incomplete UART reader supervision')
        oracle.require(all(0<=p['gap']<=.25 for p in polls) and all(b-a<=.25 for a,b in zip(stamps,stamps[1:])),'UART reader continuity gap')
        boots.append(dict(nonce=boot['nonce'],reset=boot['reset'],uart=uart,writes=jsonl(boot['writes_file']),controls=jsonl(boot['controls_file']),peer_stops=jsonl(boot['peer_stops_file']),peers={phase:jsonl(name) for phase,name in boot['peer_files'].items()}))
    result=m5_series.validate_series(dict(schema=index['schema'],source=index['source'],clock=index['clock'],boots=boots),source,abi,profile)
    if 'reset_witness' in profile:
        oracle.require(index.get('reset_semantics')==profile['reset_witness'],'Bundle does not declare the profile reset witness')
        result['reset_witness']=profile['reset_witness']
    return result


def supervised(method):
    """Every invalid event latches abort; no later action may create traffic."""
    def call(self,*args,**kwargs):
        oracle.require(not self.aborted,'Supervisor already aborted; preserve evidence, no further actions')
        try:return method(self,*args,**kwargs)
        except (ValueError,KeyError,TypeError,StopIteration,IndexError):
            self.aborted=True
            raise
    return call

class Supervisor:
    """Transport-independent single-command runner, deliberately without I/O.

    An approved adapter timestamps exact received records, feeds peer readiness,
    writes only the returned command and retains write receipts. This class does
    not authorise any physical operation. tick() must run at least every250ms;
    adapter failure/late evidence aborts rather than cancels target execution.
    """
    def __init__(self,nonce,source,started,ABI,series_deadline=None,profile=m5_series.FULL):
        oracle.require(type(nonce) is int and 0<nonce<=0xffffffff,'Invalid nonce')
        oracle.require(re.fullmatch('[0-9a-f]{12}',source) is not None,'Invalid source')
        self.profile=profile;self.nonce=nonce;self.source=source;self.abi=ABI;self.deadline=started+1800 if series_deadline is None else min(started+1800,series_deadline)
        self.last=started;self.wait=started+90;self.state='boot';self.sequence=1
        self.aborted=False;self.ticket=None;self.used=set();self.watch=None
        self.rows=[];self.peer=None;self.peer_phase=None;self.stopped_phase=None;self.request=None;self.cued=False;self.controls=[];self.up=None;self.last_link=None

    def first_traffic(self):
        return next(sequence for sequence,count,_ in self.profile['phases'] if count)

    def now(self,now):
        oracle.require(type(now) in (int,float) and self.last<=now<min(self.deadline,self.wait),'External absolute/operator deadline expired')
        self.last=now
        if self.watch:self.watch.check(now)

    @supervised
    def tick(self,now):self.now(now)

    @supervised
    def boot(self,source,now):
        self.now(now);oracle.require(self.state=='boot' and source==self.source,'Unexpected boot/source/reset')
        self.state='arm';return 'A '+str(self.nonce)+'\n'

    @supervised
    def target(self,row,now,received=None):
        self.now(now)
        text='M5 '+row['event']+' '+' '.join(str(k)+'='+str(v) for k,v in row.items() if k!='event')
        oracle.require(oracle.parse_record(text)==row and row['token']==self.nonce,'Malformed/foreign target event')
        event=row['event']
        if event=='LINK':
            m5_series.validate_primary(row);self.last_link=row['link']
            if row['link']==2:self.up=(row,now)
            if self.state=='running':self.rows.append(row)
            return
        if self.state=='arm':
            oracle.require(event=='ARM','Expected ARM');self.state='ready';return
        if event=='READY':
            oracle.require(self.state=='ready' and row['seq']==self.sequence and row['ticket']>0 and row['ticket'] not in self.used,'Missing completion/stale READY')
            self.ticket=row['ticket'];self.state='finished' if self.sequence==len(self.profile['phases'])+1 else 'prepared';self.wait=min(self.deadline,now+90);return
        oracle.require(self.state=='running' and row['seq']==self.sequence,'Unsolicited/misordered target traffic')
        self.watch.feed(event,now if received is None else received);self.rows.append(row)
        if event=='DONE':
            sequence,count,mode=self.profile['phases'][self.sequence-1]
            if count:
                result=oracle.validate_command(self.rows,self.nonce,sequence,count,mode,self.abi)
                oracle.require(result['status']=='PASS','Observed latency requires independent review')
            else:
                oracle.require([r['event'] for r in self.rows if r['event']!='LINK']==['IDLE','IDLE','IDLE','DONE'],'Incomplete idle phase')
                oracle.validate_idle(self.rows,self.nonce,sequence,0,self.rows[0]['at'])
            self.sequence+=1;self.state='ready';self.watch=None;self.peer=None;self.wait=min(self.deadline,now+90)

    @supervised
    def peer_ready(self,row,now):
        self.now(now);_,count,mode=self.profile['phases'][self.sequence-1]
        oracle.require(self.state=='prepared' and count and self.peer is None and (self.peer_phase is None or self.stopped_phase==self.peer_phase),'Unsolicited/duplicate peer readiness or previous peer not reaped')
        oracle.require(row.get('event')=='ready' and row.get('token')==self.nonce and row.get('phase')==self.sequence and row.get('mode')==mode and row.get('address')=='192.168.77.1' and row.get('tcp_port')==42420 and row.get('udp_port')==42421,'Peer readiness does not match phase')
        self.peer=row;self.peer_phase=self.sequence

    @supervised
    def peer_stopped(self,phase,reaped,now):
        self.now(now)
        oracle.require(self.state in ('ready','prepared','finished') and self.peer_phase==phase and reaped is True and self.stopped_phase!=phase,'Missing/duplicate/unreaped peer stop')
        self.stopped_phase=phase

    @supervised
    def next_command(self,now):
        self.now(now);oracle.require(self.state=='prepared' and self.ticket is not None,'No fresh readiness/no command queueing')
        sequence,count,_=self.profile['phases'][self.sequence-1]
        oracle.require(not count or self.peer is not None,'Peer must be ready before traffic')
        if sequence==self.first_traffic():oracle.require(self.last_link==2,'Configured primary UP required before initial traffic')
        if sequence==self.profile['recovery']:
            required=['cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route']
            oracle.require(len(self.controls)==6 and {r['event'] for r in self.controls}==set(required) and self.up is not None and self.last_link==2,'Recovery host/route/target UP gates incomplete')
            reconnect=next(r['monotonic'] for r in self.controls if r['event']=='cable_connect_receipt')
            oracle.require(reconnect<=self.up[1]<=now and now-reconnect<=15,'Restoration observation deadline')
        result=command(self.nonce,sequence,count,self.ticket,self.profile)
        self.used.add(self.ticket);self.ticket=None;self.rows=[];self.state='running'
        self.wait=min(self.deadline,now+150);self.watch=oracle.Watch(now,now+150,self.deadline)
        return result

    @supervised
    def command_written(self,started,now):
        self.now(now)
        oracle.require(self.state=='running' and started<=now,'No running command write')
        self.wait=min(self.deadline,started+150)
        self.watch=oracle.Watch(started,started+150,self.deadline)

    @supervised
    def peer_request(self,row,now):
        self.now(now)
        oracle.require(self.state=='running' and self.sequence==self.profile['loss'] and self.request is None,'Unexpected loss request')
        expected=__import__('struct').pack('!4sIHHI16s',b'M5N1',self.nonce,self.profile['loss'],1,1,b'crux-m5-echo-v1\0').hex()
        oracle.require(row.get('event')=='request' and row.get('token')==self.nonce and row.get('phase')==self.profile['loss'] and row.get('protocol')=='tcp' and row.get('transaction')==1 and row.get('request_hex')==expected,'Loss peer request identity mismatch')
        main=[r for r in self.rows if r['event']!='LINK']
        oracle.require(not any(r['event']=='END' and r.get('udp')==0 for r in main),'Loss TCP already completed')
        self.request=row

    @supervised
    def disconnect_cue(self,now):
        self.now(now)
        main=[r for r in self.rows if r['event']!='LINK']
        oracle.require(self.request is not None and self.sequence==self.profile['loss'] and self.state=='running' and self.watch.active and main and main[-1]['event']=='BEGIN' and main[-1]['udp']==0 and not self.cued,'No confirmed in-flight disconnect window')
        self.cued=True;record=dict(event='cable_disconnect_cue',monotonic=now);self.controls.append(record);return record

    @supervised
    def control(self,row,now):
        self.now(now)
        required=['cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route']
        seen={r['event'] for r in self.controls};event=row.get('event')
        oracle.require(event in required and event not in seen and type(row.get('monotonic')) in (int,float) and row['monotonic']<=now,'Unexpected/duplicate physical witness')
        oracle.require('cable_disconnect_cue' in seen,'No approved disconnect cue')
        if event=='cable_connect_receipt':oracle.require(self.sequence==self.profile['recovery'] and self.state=='prepared' and self.stopped_phase==self.profile['loss'] and {'host_down','cable_disconnect_receipt'}<=seen,'Reconnect requires complete L7 DONE/READY8 and independent loss witnesses')
        if event=='host_up':oracle.require('cable_connect_receipt' in seen,'UP before reconnect')
        if event=='route':oracle.require('host_up' in seen,'Route before carrier UP')
        if row['event'] in ('host_down','host_up','route'):oracle.require(row.get('interface')=='en5','Wrong dedicated NIC')
        if row['event']=='route':oracle.require((row.get('source'),row.get('destination'))==('192.168.77.1','192.168.77.2'),'Wrong route')
        self.controls.append(row)
        if event=='cable_connect_receipt':self.wait=min(self.wait,row['monotonic']+15)


def run_boot(supervisor,adapter,clock):
    """Run one already-approved fresh boot via an injected, bounded adapter.

    adapter.poll(timeout) returns ordered {kind, monotonic, record} events;
    kind is boot/target/peer_ready/peer_request/control. poll must return within
    timeout<=0.25s and retain raw bytes/poll continuity before parsing. It must
    not perform reset, program or cable operations. write_uart(line) records
    exact write bytes and shared-clock receipt, checks current ownership/guard,
    and returns promptly; it may never queue an additional command. prepare_peer
    starts the exact M5 peer, retaining its original JSONL; stop_peer is bounded
    and returns {phase, reaped: True} for a previously started process. It must
    request graceful stop, retain the final exchange, then reap/close ports. A
    missing/unreaped receipt blocks reconnect and all later peer starts.
    notify_operator only displays a cue: physical action needs separate approval.

    Every callback must itself be bounded; this synchronous core cannot cancel
    an adapter blocked inside native I/O. The operational adapter is implemented in m5_capture_adapter; its
    device/process dependencies are exercised with software fakes. Existing accepted M3 capture/probe/guard procedures remain mandatory.
    """
    peer_phase=None;primary=None;restored=False
    try:
        while supervisor.state!='finished':
            supervisor.tick(clock())
            for event in adapter.poll(.25):
                received=event['monotonic'];now=clock();row=event['record'];kind=event['kind']
                oracle.require(received<=now,'Adapter supplied future receipt timestamp')
                if kind=='boot':adapter.write_uart(supervisor.boot(row['source'],now),deadline=min(supervisor.wait,supervisor.deadline))
                elif kind=='target':supervisor.target(row,now,received=received)
                elif kind=='peer_ready':supervisor.peer_ready(row,now)
                elif kind=='peer_request':
                    if row.get('phase')==supervisor.profile['loss']:
                        supervisor.peer_request(row,now)
                elif kind=='control':supervisor.control(row,now)
                else:raise ValueError('Unknown adapter event')
            supervisor.tick(clock())
            main=[r for r in supervisor.rows if r['event']!='LINK']
            if supervisor.sequence==supervisor.profile['loss'] and supervisor.state=='running' and supervisor.request is not None and supervisor.watch.active and main and main[-1]['event']=='BEGIN' and main[-1]['udp']==0 and not supervisor.cued:
                adapter.notify_operator(supervisor.disconnect_cue(clock()))
            if supervisor.state=='prepared':
                sequence,count,mode=supervisor.profile['phases'][supervisor.sequence-1]
                if count and peer_phase!=sequence:
                    receipt=adapter.stop_peer()
                    if supervisor.peer_phase is not None and supervisor.stopped_phase!=supervisor.peer_phase:
                        oracle.require(isinstance(receipt,dict),'Missing bounded peer stop/reap receipt')
                        supervisor.peer_stopped(receipt['phase'],receipt['reaped'],clock())
                    restore=getattr(adapter,'restore_link',None)
                    # The host link returns before the recovery peer binds its sockets.
                    if restore is not None and sequence==supervisor.profile['recovery'] and supervisor.stopped_phase==supervisor.profile['loss'] and not restored:
                        restore();restored=True
                    adapter.prepare_peer(supervisor.nonce,sequence,count,mode);peer_phase=sequence
                recovery_ready=sequence!=supervisor.profile['recovery'] or ({r['event'] for r in supervisor.controls}=={'cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route'} and supervisor.up is not None)
                # The first command waits for the target's own LINK record: its print would eat the command.
                link_ready=supervisor.sequence!=1 or supervisor.last_link==2
                if (not count or supervisor.peer is not None) and recovery_ready and link_ready:
                    line=supervisor.next_command(clock())
                    started=adapter.write_uart(line,deadline=min(supervisor.watch.absolute,supervisor.watch.progress))
                    if started is not None:supervisor.command_written(started,clock())
        return dict(status='BOOT_PROTOCOL_COMPLETE_PENDING_RAW_REPLAY',nonce=supervisor.nonce)
    except BaseException as error:
        primary=error;supervisor.aborted=True
        raise
    finally:
        # stop_peer must never reset/cancel target or touch probe/cable state.
        try:adapter.stop_peer()
        except BaseException as cleanup:
            if primary is None:
                supervisor.aborted=True
                raise
            primary.evidence_failure=repr(cleanup)


def series_session(directory,prepared,boot,now,host_boot=None):
    """Persist one nonrenewable 1800s series budget across three capture CLIs."""
    path=Path(directory)/'m5-series-session.json'
    if not path.exists():
        oracle.require(boot==1,'Fresh series must start at boot1')
        oracle.require(isinstance(host_boot,str) and host_boot.strip(),'Current guard host boot identity required')
        record=dict(schema=1,host_boot=host_boot,clock=m5_series.CLOCK,manifest_sha256=prepared['manifest_sha256'],nonces=prepared['nonces'],started=now,completed=[],status='RUNNING')
        with path.open('x') as output:json.dump(record,output)
    else:record=json.loads(path.read_text())
    oracle.require(record['schema']==1 and record.get('host_boot')==host_boot and record['clock']==m5_series.CLOCK and record['manifest_sha256']==prepared['manifest_sha256'] and record['nonces']==prepared['nonces'] and record['status']=='RUNNING','Stale/aborted/foreign capture session')
    oracle.require(record['completed']==list(range(1,boot)) and record['started']<=now<record['started']+1800,'Boot order/whole-series absolute deadline')
    return path,record


def capture_approved_boot(args):
    """One concrete operational capture; caller separately authorises its same-owner reset.

    All immutable manifests and written permissions are checked before opening
    UART or probing NIC state. The continuous reader starts before the
    named reset through the accepted M3 helper in the same lease owner.
    """
    oracle.require(getattr(args,'execute_hardware',False) is True and getattr(args,'perform_approved_reset',False) is True,'Explicit physical capture and reset flags required')
    prepared=json.loads(Path(args.plan).read_text())
    if 'plan' in prepared:prepared=prepared['plan']
    approval=json.loads(Path(args.approval).read_text());validate_approval(approval,prepared)
    oracle.require(prepared['runner_sha256']==sha(__file__) and prepared['adapter_sha256']==sha(Path(__file__).with_name('m5_capture_adapter.py')),'Runner/adapter changed since approval')
    oracle.require(sha(args.entry_manifest)==prepared['entry_manifest_sha256'] and sha(args.recovery_manifest)==prepared['recovery_manifest_sha256'],'Entry/recovery manifests differ from approval')
    from prepare_m5_network import verify
    from capture_m3 import verify_manifest,verify_jlink_tools
    image=verify(args.manifest,prepared['manifest_sha256'])
    entry=verify_manifest(Path(args.entry_manifest));normal=verify_manifest(Path(args.recovery_manifest))
    oracle.require(normal['profile'] in ('normal','integrated-normal'),'Restoration image is not accepted normal role')
    verify_jlink_tools()  # Reads pinned files only; no probe access.
    from m5_capture_adapter import CaptureAdapter,M3UARTLease
    import m3_clock
    evidence=Path(args.evidence)
    oracle.require(not evidence.exists(),'Never overwrite capture evidence')
    evidence.parent.mkdir(parents=True,exist_ok=True)
    guard_session=json.loads(Path(args.guard_session).read_text())
    host_boot=guard_session.get('boot_session_uuid',guard_session.get('boot_identity'))
    session_path,session=series_session(evidence.parent,prepared,args.boot,m3_clock.monotonic(),host_boot)
    safety=evidence.parent/('boot-'+str(args.boot)+'-safety');safety.mkdir(exist_ok=False)
    lease=M3UARTLease(guard_session,safety,image_binary=image['files']['binary'],image_sha256=sha(image['files']['binary']))
    def reverify():
        verify(args.manifest,prepared['manifest_sha256'])
        oracle.require(sha(args.entry_manifest)==prepared['entry_manifest_sha256'] and sha(args.recovery_manifest)==prepared['recovery_manifest_sha256'],'Recovery artefact changed')
        verify_manifest(Path(args.entry_manifest));verify_manifest(Path(args.recovery_manifest))
    adapter=CaptureAdapter(prepared,approval,args.boot,evidence,lease,manifest_check=reverify)
    primary=None
    try:
        adapter.perform_approved_reset(deadline=session['started']+1800)
        supervisor=Supervisor(prepared['nonces'][args.boot-1],image['source']['git_commit'][:12],m3_clock.monotonic(),image['errno_values'],series_deadline=session['started']+1800)
        result=run_boot(supervisor,adapter,m3_clock.monotonic)
        adapter.emit('result.jsonl',result)
        adapter.close()
        session['completed'].append(args.boot)
        if args.boot==3:session['status']='PROTOCOL_COMPLETE_PENDING_RAW_REPLAY'
        session_path.write_text(json.dumps(session,indent=2)+'\n')
        return result
    except BaseException as error:
        primary=error
        session['status']='ABORT_INCONCLUSIVE';session_path.write_text(json.dumps(session,indent=2)+'\n')
        adapter.emit('result.jsonl',dict(status='ABORT_INCONCLUSIVE',error=repr(error),target_may_still_execute=True,automatic_reset=False))
        raise
    finally:
        try:adapter.close()
        except BaseException as cleanup:
            if primary is None:raise
            primary.evidence_failure=repr(cleanup)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='mode',required=True)
    p=sub.add_parser('plan');p.add_argument('--manifest',required=True);p.add_argument('--entry-manifest',required=True);p.add_argument('--recovery-manifest',required=True);p.add_argument('--nonces',type=int,nargs=3,required=True)
    p=sub.add_parser('check-approval');p.add_argument('--plan',required=True);p.add_argument('--approval',required=True)
    p=sub.add_parser('uart-replay');p.add_argument('directory')
    p=sub.add_parser('replay');p.add_argument('--series',required=True,help='Raw evidence bundle index, not embedded UART JSON');p.add_argument('--source',required=True);p.add_argument('--abi',required=True)
    p=sub.add_parser('capture',help='Approved one-boot capture and separately flagged reset; no programming or cable control')
    p.add_argument('--execute-hardware',action='store_true',required=True)
    p.add_argument('--perform-approved-reset',action='store_true',required=True)
    for name in ('plan','approval','manifest','entry-manifest','recovery-manifest','guard-session','evidence'):
        p.add_argument('--'+name,required=True)
    p.add_argument('--boot',type=int,choices=(1,2,3),required=True)
    args=parser.parse_args()
    if args.mode=='capture':
        result=capture_approved_boot(args)
    elif args.mode=='plan':
        prepared=plan(sha(args.manifest),args.nonces,sha(args.entry_manifest),sha(args.recovery_manifest));result=dict(plan=prepared,approval_template=approval_template(prepared))
    elif args.mode=='check-approval':result=validate_approval(json.loads(Path(args.approval).read_text()),json.loads(Path(args.plan).read_text()))
    elif args.mode=='uart-replay':result=uart_records(args.directory)
    else:result=replay_bundle(args.series,args.source,json.loads(Path(args.abi).read_text()))
    print(json.dumps(result,indent=2,sort_keys=True))

if __name__=='__main__':main()

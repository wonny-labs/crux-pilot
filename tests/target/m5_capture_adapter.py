"""Operational M5 UART/peer/operator/NIC adapter, inert until constructed.

Programming and the three fresh resets use separately approved M3 operations.
Construct this adapter before the corresponding reset so its continuous reader
retains the boot. Only perform_approved_reset uses the separately authorised same-owner reset; no method programs, restores, or switches a cable.
"""
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import m3_clock as shared_clock
import capture_m5_network as runner


def record_operator_receipt(directory,event,*,confirmed=False,clock=shared_clock.monotonic):
    """Record a completed, separately authorised manual cable operation only.

    This annotation performs no cable/NIC operation and is insufficient proof by
    itself: the runner additionally requires native host and target observations.
    The operator must select the exact current capture directory after the cue.
    """
    runner.oracle.require(confirmed is True and event in ('cable_disconnect_receipt','cable_connect_receipt'),'Explicit completed-operation confirmation required')
    directory=Path(directory)
    starts=[json.loads(line) for line in (directory/'adapter.jsonl').read_text().splitlines()]
    runner.oracle.require(len([r for r in starts if r.get('event')=='capture_started'])==1,'Capture identity unavailable')
    path=directory/'operator.jsonl'
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    runner.oracle.require(not any(row['event']==event for row in rows),'Duplicate operator receipt')
    runner.oracle.require(event!='cable_connect_receipt' or any(row['event']=='cable_disconnect_receipt' for row in rows),'Reconnect receipt before disconnect')
    with path.open('a') as output:output.write(json.dumps(dict(event=event,monotonic=clock()))+'\n')


class ProcessBackend:
    """Owned peer processes only; no shell, inherited devices or process policy."""
    def start_peer(self,nonce,phase,count,mode,output,log):
        with log.open('xb') as stream:
            return subprocess.Popen([sys.executable,str(Path(__file__).with_name('m5_network_peer.py')),'--token',str(nonce),'--phase',str(phase),'--count',str(count),'--mode',mode,'--output',str(output)],stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)

    def stop_peer(self,process):
        code=process.poll()
        if code is None:
            process.send_signal(signal.SIGTERM)
            try:code=process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL)
                try:process.wait(timeout=1)
                except subprocess.TimeoutExpired:pass
                raise ValueError('Peer grace deadline exceeded; qualification aborted')
        else:code=process.wait(timeout=.1)
        runner.oracle.require(code==0,'Peer failed; raw logs retained')
        return code


def parse_interface(text):
    """Observed dedicated IPv4 address/carrier; ambiguous inventory is unknown."""
    addresses=re.findall(r'(?m)^[ \t]*inet ([0-9.]+) netmask (0x[0-9a-f]+)\b',text)
    runner.oracle.require(addresses==[('192.168.77.1','0xffffff00')] and
        re.findall(r'(?m)^[ \t]*ether ([0-9a-f:]+)[ \t]*$',text)==['00:0a:cd:41:89:cc'],
        'NIC identity/static address mismatch')
    statuses=re.findall(r'(?m)^[ \t]*status:[ \t]*(\S+)[ \t]*$',text)
    runner.oracle.require(statuses in (['active'],['inactive']),'NIC carrier status missing/unknown/ambiguous')
    return dict(source=addresses[0][0],active=statuses==['active'])


def parse_direct_route(text,source):
    """Narrow Apple route(8) get format, not a route-selection/binding API.

    print_getmsg prints gateway only for RTF_GATEWAY. A connected /24 or
    cloned /32 is accepted; next-hop, reject, blackhole and unknown forms stop.
    """
    def field(name,optional=False):
        values=re.findall(r'(?m)^[ \t]*'+re.escape(name)+r':[ \t]*([^\n]*?)[ \t]*$',text)
        runner.oracle.require(len(values)==1 or (optional and not values),'Missing/duplicate route field: '+name)
        return values[0] if values else None
    lookup=field('route to');destination=field('destination');mask=field('mask',True)
    interface=field('interface');flags_text=field('flags')
    runner.oracle.require(field('gateway',True) is None,'Gateway route is outside isolated topology')
    match=re.fullmatch(r'<([A-Z0-9_,]+)>',flags_text)
    runner.oracle.require(match is not None,'Unknown route flag format')
    flags=match[1].split(',')
    allowed={'UP','DONE','HOST','STATIC','CLONING','PRCLONING','LLINFO','WASCLONED','IFSCOPE','IFREF','PINNED'}
    runner.oracle.require(len(flags)==len(set(flags)) and set(flags)<=allowed and 'UP' in flags,
                         'Gateway/reject/blackhole/unknown or inactive route')
    runner.oracle.require(source=='192.168.77.1' and lookup=='192.168.77.2' and interface=='en5','Isolated route source/destination/interface mismatch')
    if 'HOST' in flags:
        runner.oracle.require(destination==lookup and mask in (None,'255.255.255.255'),'Unexpected direct host route')
    else:
        runner.oracle.require(destination=='192.168.77.0' and mask=='255.255.255.0','Unexpected direct network route')
    return dict(event='route',interface=interface,source=source,destination=lookup,
                route_destination=destination,route_mask=mask,route_flags=flags,gateway=None,
                source_basis='validated ifconfig IPv4 address')


class NICProbe:
    """Nonblocking dedicated-NIC inventory; process lifetime limited to one second."""
    def __init__(self,directory,clock=shared_clock.monotonic):
        self.directory=directory;self.clock=clock;self.process=None;self.serial=0;self.route=False;self.interface_observation=None
    def start(self,route=False):
        runner.oracle.require(self.process is None,'NIC inventory already running')
        self.serial+=1;self.route=route
        command=['route','-n','get','192.168.77.2'] if route else ['ifconfig','en5']
        self.path=self.directory/('nic-'+str(self.serial)+'.txt');self.stream=self.path.open('xb')
        self.process=subprocess.Popen(command,stdout=self.stream,stderr=subprocess.STDOUT,start_new_session=True);self.deadline=self.clock()+1
    def poll(self):
        if self.process is None:return None
        code=self.process.poll()
        if code is None:
            if self.clock()<self.deadline:return None
            self.process.kill()
            try:self.process.wait(timeout=.1)
            except subprocess.TimeoutExpired:pass
            self.stream.close();self.process=None;raise ValueError('NIC inventory deadline; state unknown')
        self.process.wait(timeout=.1);self.stream.close();self.process=None
        text=self.path.read_text();runner.oracle.require(code==0,'NIC inventory failed')
        if self.route:
            observed=self.interface_observation
            runner.oracle.require(observed is not None and observed['active'] and self.clock()-observed['at']<=2,
                                  'Fresh validated dedicated source observation required')
            return parse_direct_route(text,observed['source'])
        observed=parse_interface(text);self.interface_observation=dict(observed,at=self.clock())
        return dict(event='host_up' if observed['active'] else 'host_down',interface='en5')
    def close(self):
        if self.process is not None:
            self.process.kill()
            try:self.process.wait(timeout=.1)
            except subprocess.TimeoutExpired:pass
            self.stream.close();self.process=None


class M3UARTLease:
    """Concrete accepted-M3 guard, identity and exclusive UART lease.

    This is created without device access. __call__ opens only after the adapter
    has validated exact physical approval. The root-created mount guard is an
    external prerequisite and is never created/escalated by this runner.
    """
    def __init__(self,guard_session,directory,uart='/dev/cu.usbmodem11302',*,image_binary=None,image_sha256=None):
        self.session=guard_session;self.directory=Path(directory);self.uart=uart;self.image_binary=Path(image_binary).resolve() if image_binary else None;self.image_sha256=image_sha256;self.port=None;self.lock=None;self.count=0
    def verify(self,operation,remaining=90):
        import capture_m3_network as m3
        import capture_m3
        self.count+=1;label='lease-'+str(self.count)
        m3.guard_check(self.session,self.directory,label,remaining)
        if self.port is None:
            capture_m3.check_owners(self.uart,self.directory/(label+'-owners.txt'))
            runner.oracle.require(m3.check_route(self.directory,label),'Initial isolated NIC not active')
            interface=m3.run_checked(['ifconfig','en5'],self.directory/(label+'-carrier.txt'))
            observed=parse_interface(interface)
            runner.oracle.require(observed['active'],'Initial carrier unknown/inactive')
            parse_direct_route((self.directory/(label+'-route.txt')).read_text(),observed['source'])
            service=m3.run_checked(['networksetup','-getinfo','AX88179'],self.directory/(label+'-service.txt'))
            runner.oracle.require('IP address: 192.168.77.1' in service and 'Subnet mask: 255.255.255.0' in service and 'Router: (null)' in service and 'Ethernet Address: 00:0a:cd:41:89:cc' in service,'Isolated service changed')
        else:
            # The accepted owner is this process only; deny every other UART
            # owner and all debugger processes while this capture owns UART.
            result=capture_m3.inventory(['lsof','-Fp',self.uart,self.uart.replace('/dev/cu.','/dev/tty.')],self.directory/(label+'-owners-timeout.json'))
            runner.oracle.require(result.returncode==0 and not result.stderr and result.stdout.splitlines() and all(line=='p'+str(os.getpid()) for line in result.stdout.splitlines()),'Exclusive UART ownership changed')
            result=capture_m3.inventory(['ps','-axo','pid=,comm=,args='],self.directory/(label+'-probe-timeout.json'))
            runner.oracle.require(result.returncode==0 and not result.stderr and result.stdout.strip() and all(re.match(r'^\s*\d+\s+\S+',line) for line in result.stdout.splitlines()) and not re.search(r'(?im)(?:/|\s)(?:JLink\S*|openocd|pyocd(?:\.\S+)?)(?:\s|$)',result.stdout),'Probe ownership changed')
    def __call__(self):
        import fcntl
        import serial
        from serial.tools import list_ports
        import capture_m3
        runner.oracle.require(self.uart=='/dev/cu.usbmodem11302','UART differs from accepted M3 inventory')
        self.verify('acquire_exclusive_probe_uart_and_nic')
        capture_m3.select_uart([dict(device=p.device,serial_number=p.serial_number,vid=p.vid,pid=p.pid) for p in list_ports.comports()],self.uart,runner.UART_USB)
        self.lock=(self.directory.parent/'evkb-602003106.lock').open('a')
        fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        self.port=serial.Serial(self.uart,115200,timeout=.02,write_timeout=.25,exclusive=True)
        import termios
        fcntl.ioctl(self.port.fileno(),termios.TIOCEXCL)
        return self.port
    def reset(self,boot,output,*,deadline,clock=shared_clock.monotonic,record=lambda _:None):
        """Read-only identity/image preflight, then separately approved reset."""
        from capture_m3 import commander
        binary=self.image_binary
        runner.oracle.require(binary is not None and binary.is_file() and runner.sha(binary)==self.image_sha256,'Exact approved current M5 BIN is unavailable/changed')
        runner.oracle.require('"' not in str(binary) and '\n' not in str(binary),'Unsafe verification path')
        def target(commands,label,path,starting=None):
            runner.oracle.require(clock()+120<deadline,'Insufficient whole-series preflight/reset/cleanup reserve')
            self.verify(label,90)
            runner.oracle.require(clock()+45<deadline,'Reset reserve expired during guard; no probe command')
            if starting:starting()
            text=commander(runner.PROBE,commands,path)
            runner.oracle.require('CPUID register: 0x411FC271' in text and 'DPIDR: 0x0BD11477' in text and re.search(r'S/N:\s*602003106\b',text) and 'Cortex-M7' in text,'Target/probe identity mismatch before further operations')
            voltage=re.search(r'VTref\s*=\s*([\d.]+)\s*V',text)
            runner.oracle.require(voltage and 3<=float(voltage[1])<=3.6,'Target voltage mismatch; no further operations')
            return text
        output=Path(output);preflight='qualification_preflight_'+str(boot)
        target('q\n',preflight,output.with_name('preflight-identity.log'))
        runner.oracle.require(runner.sha(binary)==self.image_sha256,'M5 BIN changed during preflight')
        text=target('verifybin "'+str(binary)+'", 0x60000000\nq\n',preflight,output.with_name('preflight-current-image.log'))
        runner.oracle.require('Verify successful' in text,'Current flash contents not verified; no reset')
        record('approved_preflight_completed')
        text=target('r\ng\nq\n','qualification_reset_'+str(boot),output,starting=lambda:record('approved_reset_started'))
        record('approved_reset_completed')
        return text
    def close(self):
        try:
            if self.port is not None:self.port.close();self.port=None
        finally:
            if self.lock:self.lock.close();self.lock=None


class CaptureAdapter:
    """Actual capture/peer loop with injectable device/process dependencies.

    operator.jsonl is written by the separately authorised operator with only
    cable_disconnect_receipt/cable_connect_receipt records and shared host
    CLOCK_MONOTONIC timestamps. It never directs an automatic cable operation.
    Native NIC observations are generated and retained by this adapter.
    """
    def __init__(self,prepared,approval,boot,directory,uart_factory,*,receiver_factory=None,process_backend=None,nic_factory=NICProbe,clock=shared_clock.monotonic,software_only=False,manifest_check=None):
        runner.validate_approval(approval,prepared)
        runner.oracle.require(software_only or callable(manifest_check),'Live adapter requires exact immutable manifest verification')
        if manifest_check:manifest_check()
        if 'plan' in prepared:prepared=prepared['plan']
        runner.oracle.require(boot in (1,2,3),'Invalid fresh boot number')
        self.prepared=prepared;self.boot=boot;self.nonce=prepared['nonces'][boot-1];self.clock=clock;self.factory=uart_factory
        self.safety=getattr(uart_factory,'verify',None)
        runner.oracle.require(software_only or callable(self.safety),'Live adapter requires accepted M3 guard/ownership lease')
        self.directory=Path(directory);self.directory.mkdir(parents=True,exist_ok=False)
        self.backend=process_backend or ProcessBackend();self.process=None;self.phase=None;self.peer_path=None;self.tail={};self.pending=b'';self.line_offset=0;self.reset_offset=None;self.qualifying_started=False;self.boot_seen=False;self.closed=False
        self.nic=nic_factory(self.directory,clock);self.nic_want=None;self.nic_due=0;self.nic_running=False
        self.operator_path=self.directory/'operator.jsonl';self.operator_path.touch()
        self.port=None;self.receiver=None
        try:
            if self.safety:self.safety('acquire_exclusive_probe_uart_and_nic',90)
            self.port=uart_factory()
            runner.oracle.require(0<=self.port.timeout<=.02 and 0<self.port.write_timeout<=.25,'UART read/write bounds differ from approved adapter')
            if receiver_factory is None:
                from capture_m3 import Receiver
                receiver_factory=Receiver
            if not software_only:
                from capture_m3 import drain
                drain(self.port,self.directory)
            self.receiver=receiver_factory(self.port,self.directory);self.receiver.begin()
            self.emit('adapter.jsonl',dict(event='capture_started',boot=boot,nonce=self.nonce,software_only=software_only))
        except BaseException as primary:
            try:self.close()
            except BaseException as cleanup:primary.evidence_failure=repr(cleanup)
            raise
    def perform_approved_reset(self,*,deadline):
        operation='qualification_reset_'+str(self.boot)
        runner.oracle.require(not getattr(self,'reset_done',False) and self.prepared['operations'].count(operation)==1 and self.prepared['operations'].count('qualification_preflight_'+str(self.boot))==1,'Unexpected/repeated fresh reset')
        reset=getattr(self.factory,'reset',None)
        runner.oracle.require(callable(reset),'Accepted same-owner reset unavailable')
        self.reset_done=True
        preflight='qualification_preflight_'+str(self.boot)
        self.emit('operations.jsonl',dict(event='approved_preflight_started',operation=preflight))
        def record(event):
            if event=='approved_reset_started':self.mark_reset_boundary()
            self.emit('operations.jsonl',dict(event=event,operation=preflight if 'preflight' in event else operation))
        reset(self.boot,self.directory/'qualification-reset.log',deadline=deadline,clock=self.clock,record=record)

    def mark_reset_boundary(self):
        """Validate/retain passive old output before the actual reset command."""
        import m3_uart
        deadline=self.clock()+.25
        while True:
            try:rows=m3_uart.exact_event_rows(self.directory);break
            except ValueError:
                runner.oracle.require(self.clock()<deadline,'Incomplete old UART at reset boundary')
                shared_clock.sleep(.005)
        for _,line in rows:runner.passive_prefix_line(line)
        self.reset_offset=(self.directory/'uart-rx.bin').stat().st_size
        (self.directory/'reset-boundary.json').write_text(json.dumps(dict(raw_offset=self.reset_offset,monotonic=self.clock()))+'\n')

    def emit(self,name,row):
        record=dict(row);record.setdefault('monotonic',self.clock())
        with (self.directory/name).open('a') as output:output.write(json.dumps(record)+'\n')
    def lines(self,path):
        offset,pending=self.tail.get(str(path),(0,b''))
        if not path.exists():return []
        with path.open('rb') as stream:
            stream.seek(offset);chunk=stream.read()
        pending+=chunk;complete=pending.split(b'\n');self.tail[str(path)]=(offset+len(chunk),complete.pop())
        return [json.loads(line) for line in complete]
    def target_events(self):
        result=[]
        for event in self.lines(self.directory/'receive.jsonl'):
            self.pending+=bytes.fromhex(event['hex'])
            while b'\n' in self.pending:
                line,self.pending=self.pending.split(b'\n',1)
                offset=self.line_offset;self.line_offset+=len(line)+1
                if line.endswith(b'\r'):line=line[:-1]
                line=line.decode('ascii')
                if self.reset_offset is not None and not self.qualifying_started:
                    if offset>=self.reset_offset and re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*',line):
                        self.qualifying_started=True
                        raw=(self.directory/'uart-rx.bin').read_bytes()
                        (self.directory/'prefix-boundary.json').write_text(json.dumps(dict(raw_start=0,raw_end=offset,sha256=runner.hashlib.sha256(raw[:offset]).hexdigest(),reason='passive UART before approved fresh reset boot'))+'\n')
                    else:
                        runner.passive_prefix_line(line);continue
                if line.startswith('M5_BOOT '):
                    match=re.fullmatch(r'M5_BOOT source=([0-9a-f]{12})',line)
                    runner.oracle.require(match and not self.boot_seen,'Malformed/duplicate M5 source boot')
                    self.boot_seen=True;row=dict(source=match[1]);kind='boot'
                elif line.startswith('M5 '):row=runner.oracle.parse_record(line);kind='target'
                else:
                    runner.oracle.require(not self.boot_seen and (line=='' or re.fullmatch(r'\*\*\* Booting Zephyr OS build [A-Za-z0-9.\-]+ \*\*\*',line)),'Unexpected raw target output')
                    continue
                result.append(dict(kind=kind,monotonic=event['monotonic'],record=row))
        return result
    def poll(self,timeout):
        runner.oracle.require(0<=timeout<=.25,'Unbounded adapter poll')
        if self.receiver.failure:raise ValueError('Continuous UART reader failed') from self.receiver.failure
        result=self.target_events()
        if self.peer_path:
            for row in self.lines(self.peer_path):
                if row['event'] in ('ready','request'):result.append(dict(kind='peer_ready' if row['event']=='ready' else 'peer_request',monotonic=row['monotonic'],record=row))
            code=self.process.poll()
            runner.oracle.require(code is None or code==0,'Peer failed during capture')
        for row in self.lines(self.operator_path):
            runner.oracle.require(set(row)=={'event','monotonic'} and row['event'] in ('cable_disconnect_receipt','cable_connect_receipt'),'Unexpected operator instruction')
            runner.oracle.require(row['monotonic']<=self.clock() and row['monotonic']>=0,'Invalid operator receipt clock')
            result.append(dict(kind='control',monotonic=row['monotonic'],record=row));self.emit('controls.jsonl',row)
            self.nic_want='host_down' if row['event']=='cable_disconnect_receipt' else 'host_up';self.nic_due=self.clock()
        if self.nic_running:
            row=self.nic.poll()
            if row:
                self.nic_running=False
                if row['event']==self.nic_want:
                    row['monotonic']=self.clock();self.emit('controls.jsonl',row);result.append(dict(kind='control',monotonic=row['monotonic'],record=row))
                    self.nic_want='route' if row['event']=='host_up' else None
                self.nic_due=self.clock()+.1
        if self.nic_want and not self.nic_running and self.clock()>=self.nic_due:
            self.nic.start(route=self.nic_want=='route');self.nic_running=True
        if not result and timeout:shared_clock.sleep(min(timeout,.02))
        return sorted(result,key=lambda row:row['monotonic'])
    def write_uart(self,line,*,deadline):
        runner.oracle.require(re.fullmatch(r'(?:A [0-9]+|[IL] [0-9]+ [0-9]+ [0-9]+|P [0-9]+ [0-9]+ [0-9]+ [0-9]+)\n',line),'Malformed runner command')
        runner.oracle.require(self.clock()<deadline,'Command deadline expired before guard')
        if self.safety:self.safety('acquire_exclusive_probe_uart_and_nic',max(.25,min(10,deadline-self.clock())))
        runner.oracle.require(self.clock()<deadline,'Command deadline expired during guard; no UART write')
        started=self.clock();data=line.encode('ascii')
        self.emit('write-attempts.jsonl',dict(line=line,monotonic=started,deadline=deadline))
        runner.oracle.require(self.port.write(data)==len(data),'Incomplete UART write; no retry')
        self.emit('writes.jsonl',dict(line=line,monotonic=started))
        return started
    def prepare_peer(self,nonce,phase,count,mode):
        runner.oracle.require(self.process is None and nonce==self.nonce,'Prior peer not reaped or wrong nonce')
        self.phase=phase;self.peer_path=self.directory/('peer-'+str(phase)+'.jsonl')
        self.process=self.backend.start_peer(nonce,phase,count,mode,self.peer_path,self.directory/('peer-'+str(phase)+'-process.log'))
    def stop_peer(self):
        if self.process is None:return None
        phase=self.phase;self.backend.stop_peer(self.process)
        runner.oracle.require(self.process.poll()==0,'Peer not reaped successfully')
        # Flush final peer records before releasing ports; file remains immutable.
        if self.peer_path:
            self.lines(self.peer_path)  # Process reaped: final buffered line is now complete or invalid.
            runner.oracle.require(not self.tail.get(str(self.peer_path),(0,b''))[1],'Incomplete peer JSONL')
        receipt=dict(phase=phase,reaped=True);self.emit('peer-stops.jsonl',receipt)
        self.process=None;self.peer_path=None;return receipt
    def notify_operator(self,row):
        self.emit('controls.jsonl',row);self.emit('operator-cues.jsonl',dict(**row,operation='boot_'+str(self.boot)+'_cable_disconnect'))
    def close(self):
        if self.closed:return
        self.closed=True;errors=[]
        actions=[self.stop_peer]
        if self.receiver is not None:actions.append(self.receiver.finish)
        if self.port is not None:actions.append(self.port.close)
        actions.append(self.nic.close)
        close=getattr(self.factory,'close',None)
        if close:actions.append(close)
        for action in actions:
            try:action()
            except BaseException as error:errors.append(error)
        if self.pending:errors.append(ValueError('Incomplete UART line'))
        if errors:
            errors[0].cleanup_failures=[repr(error) for error in errors[1:]]
            raise errors[0]

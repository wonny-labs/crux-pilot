"""Focused one-boot adapter: host NIC control, pre-banner prefix cut, exact replay. No hardware."""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import capture_m5_network as runner
import m5_focused_adapter as focused
import capture_m5_focused as focused_cli

NIC_TEXT='en5: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n\tether 00:0a:cd:41:89:cc\n\tinet 192.168.77.1 netmask 0xffffff00 broadcast 192.168.77.255\n\tstatus: %s\n'
ROUTE_TEXT='   route to: 192.168.77.2\ndestination: 192.168.77.0\n       mask: 255.255.255.0\n  interface: en5\n      flags: <UP,DONE,CLONING,STATIC>\n'
BANNER=b'*** Booting Zephyr OS build v4.1.0 ***\r\n'
BOOT=b'M5_BOOT source=123456789abc\r\n'


class Time:
    def __init__(self):self.value=1.
    def __call__(self):return self.value
    def sleep(self,seconds):self.value+=seconds


class Host:
    """Fake host: ifconfig/route/sudo with a small NIC model."""
    def __init__(self,time,carrier_delay=0.,sudo_rc=0,stuck=False):
        self.time=time;self.calls=[];self.admin=True;self.carrier_delay=carrier_delay;self.up_at=None;self.sudo_rc=sudo_rc;self.stuck=stuck
    def __call__(self,argv,**kwargs):
        self.calls.append(list(argv))
        class Result:pass
        result=Result();result.returncode=0;result.stdout=''
        if argv[0]=='sudo':
            result.returncode=self.sudo_rc
            if self.sudo_rc==0:
                self.admin=argv[-1]=='up'
                if self.admin:self.up_at=self.time()+self.carrier_delay
        elif argv[:2]==['ifconfig','en5']:
            active=self.admin and (self.up_at is None or self.time()>=self.up_at) and not self.stuck
            result.stdout=NIC_TEXT%('active' if active else 'inactive')
        elif argv[:3]==['route','-n','get']:result.stdout=ROUTE_TEXT
        else:raise AssertionError(argv)
        return result


class HostLink(unittest.TestCase):
    def link(self,**kwargs):
        time=Time();host=Host(time,**kwargs)
        return focused.HostLink(run=host,clock=time,sleep=time.sleep),host,time

    def test_down_uses_noninteractive_sudo_and_two_independent_witnesses(self):
        link,host,time=self.link()
        rows=link.down()
        self.assertEqual([r['event'] for r in rows],['host_down','cable_disconnect_receipt'])
        self.assertEqual(rows[0]['interface'],'en5')
        self.assertLessEqual(rows[0]['monotonic'],rows[1]['monotonic'])
        sudo=[c for c in host.calls if c[0]=='sudo']
        self.assertEqual(sudo,[['sudo','-n','/sbin/ifconfig','en5','down']])

    def test_down_stops_when_sudo_would_prompt(self):
        link,host,time=self.link(sudo_rc=1)
        with self.assertRaises(ValueError):link.down()
        self.assertEqual(len(host.calls),1)

    def test_down_requires_an_inactive_carrier_observation(self):
        time=Time();host=Host(time);host.admin=True
        original=host.__call__
        def never_down(argv,**kwargs):
            result=original(argv,**kwargs)
            if argv[:2]==['ifconfig','en5']:result.stdout=NIC_TEXT%'active'
            return result
        link=focused.HostLink(run=never_down,clock=time,sleep=time.sleep)
        with self.assertRaises(ValueError):link.down()

    def test_up_orders_carrier_interface_and_route_witnesses(self):
        link,host,time=self.link(carrier_delay=1.5)
        rows=link.up()
        self.assertEqual([r['event'] for r in rows],['cable_connect_receipt','host_up','route'])
        stamps=[r['monotonic'] for r in rows]
        self.assertEqual(stamps,sorted(stamps))
        self.assertGreaterEqual(stamps[0],2.5)
        self.assertEqual((rows[2]['source'],rows[2]['destination'],rows[2]['interface']),('192.168.77.1','192.168.77.2','en5'))
        self.assertEqual([c for c in host.calls if c[0]=='sudo'],[['sudo','-n','/sbin/ifconfig','en5','up']])

    def test_up_without_carrier_times_out(self):
        link,host,time=self.link(stuck=True)
        with self.assertRaises(ValueError):link.up()


class Port:
    timeout=.02;write_timeout=.25
    def __init__(self):self.writes=[];self.closed=False
    def write(self,data):self.writes.append(data);return len(data)
    def close(self):self.closed=True


class Receiver:
    failure=None
    def __init__(self,port,directory):self.directory=directory
    def begin(self):
        for name in ('uart-rx.bin','receive.jsonl','poll.jsonl'):(self.directory/name).write_bytes(b'')
    def finish(self):pass


class Process:
    stopped=False
    def poll(self):return 0 if self.stopped else None
    def send_signal(self,_):self.stopped=True
    def wait(self,timeout):return 0


class Backend:
    def start_peer(self,nonce,phase,count,mode,output,log):
        output.write_text(json.dumps(dict(event='ready',clock=runner.m5_series.CLOCK,monotonic=1.,token=nonce,phase=phase,mode=mode,address='192.168.77.1',tcp_port=42420,udp_port=42421))+'\n');return Process()
    def stop_peer(self,process):process.send_signal(15);return process.wait(3)


class FakeLink:
    def __init__(self):self.calls=[]
    def down(self):
        self.calls.append('down');return [dict(event='host_down',monotonic=2.,interface='en5'),dict(event='cable_disconnect_receipt',monotonic=2.1)]
    def up(self):
        self.calls.append('up');return [dict(event='cable_connect_receipt',monotonic=3.),dict(event='host_up',monotonic=3.1,interface='en5'),dict(event='route',monotonic=3.2,interface='en5',source='192.168.77.1',destination='192.168.77.2')]


class Adapter(unittest.TestCase):
    def make(self,root):
        port=Port();link=FakeLink()
        live=focused.FocusedAdapter(31,root/'boot',lambda:port,receiver_factory=Receiver,process_backend=Backend(),link=link,clock=Time(),software_only=True)
        return live,port,link

    def feed(self,live,*chunks):
        raw=b''.join(chunks);receive=[]
        with (live.directory/'receive.jsonl').open('a') as out:
            for number,chunk in enumerate(chunks):out.write(json.dumps(dict(monotonic=1.+number/1000,hex=chunk.hex()))+'\n')
        with (live.directory/'uart-rx.bin').open('ab') as out:out.write(raw)

    def test_prefix_before_banner_is_retained_hashed_and_not_parsed(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory));junk=b'M3_NET_STATE monotonic_ms=1 link=1\r\n'
            self.feed(live,junk[:10],junk[10:]+BANNER[:12],BANNER[12:]+BOOT+b'M5 ARM token=31 at=0\r\n')
            events=live.poll(0)
            self.assertEqual([e['kind'] for e in events],['boot','target'])
            boundary=json.loads((live.directory/'focused-prefix.json').read_text())
            self.assertEqual(boundary,dict(raw_start=0,raw_end=len(junk),sha256=hashlib.sha256(junk).hexdigest(),reason='pre-banner bytes retained, not parsed'))

    def test_junk_sharing_a_line_with_the_banner_is_cut_at_the_banner(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory));junk=b'M3_NET_STATE monotonic_ms=1 li'
            self.feed(live,junk+BANNER+BOOT)
            self.assertEqual([e['kind'] for e in live.poll(0)],['boot'])
            self.assertEqual(json.loads((live.directory/'focused-prefix.json').read_text())['raw_end'],len(junk))

    def test_no_prefix_gives_an_empty_retained_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            self.feed(live,BANNER+BOOT)
            live.poll(0)
            boundary=json.loads((live.directory/'focused-prefix.json').read_text())
            self.assertEqual((boundary['raw_end'],boundary['sha256']),(0,hashlib.sha256(b'').hexdigest()))

    def test_active_looking_or_oversized_prefix_is_rejected(self):
        for prefix in [b'M5 ARM token=31 at=0\r\n',b'M5_BOOT source=123456789abc\r\n',b'x'*5000]:
            with self.subTest(prefix=prefix[:12]),tempfile.TemporaryDirectory() as directory:
                live,port,link=self.make(Path(directory))
                self.feed(live,prefix,BANNER+BOOT)
                with self.assertRaises(ValueError):live.poll(0)

    def test_unknown_output_after_the_banner_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            self.feed(live,BANNER+BOOT+b'surprise\r\n')
            with self.assertRaises(ValueError):live.poll(0)

    def test_cue_runs_host_link_down_and_delivers_witnesses_through_poll(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            live.notify_operator(dict(event='cable_disconnect_cue',monotonic=1.5))
            self.assertEqual(link.calls,['down'])
            events=live.poll(0)
            self.assertEqual([(e['kind'],e['record']['event']) for e in events],[('control','host_down'),('control','cable_disconnect_receipt')])
            self.assertEqual(live.poll(0),[])
            rows=[json.loads(l) for l in (live.directory/'controls.jsonl').read_text().splitlines()]
            self.assertEqual([r['event'] for r in rows],['cable_disconnect_cue','host_down','cable_disconnect_receipt'])

    def test_link_down_flag_tracks_the_host_nic_so_failures_can_restore_it(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            self.assertFalse(live.link_down)
            live.notify_operator(dict(event='cable_disconnect_cue',monotonic=1.5))
            self.assertTrue(live.link_down)
            live.restore_link()
            self.assertFalse(live.link_down)

    def test_a_failed_down_still_marks_the_link_for_restoration(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            def broken():raise ValueError('carrier did not become inactive')
            link.down=broken
            with self.assertRaises(ValueError):live.notify_operator(dict(event='cable_disconnect_cue',monotonic=1.5))
            self.assertTrue(live.link_down)

    def test_restore_link_delivers_reconnect_witnesses_once(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            live.restore_link()
            self.assertEqual([e['record']['event'] for e in live.poll(0)],['cable_connect_receipt','host_up','route'])
            self.assertEqual(link.calls,['up'])

    def test_only_runner_commands_reach_the_uart(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            live.write_uart('A 31\n',deadline=live.clock()+1)
            self.assertEqual(port.writes,[b'A 31\n'])
            for line in ['reboot\n','A 31','L 31 3\n']:
                with self.subTest(line=line),self.assertRaises(ValueError):live.write_uart(line,deadline=live.clock()+1)
            self.assertEqual(len(port.writes),1)

    def test_wait_for_boot_returns_the_boot_event_and_replays_it_to_the_loop(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory));clock=live.clock
            self.assertIsNone(live.wait_for_boot(deadline=clock()+.05,sleep=clock.sleep))
            self.feed(live,BANNER+BOOT)
            event=live.wait_for_boot(deadline=clock()+5,sleep=clock.sleep)
            self.assertEqual((event['kind'],event['record']),('boot',dict(source='123456789abc')))
            self.assertEqual([e['kind'] for e in live.poll(0)],['boot'])
            self.assertEqual(live.poll(0),[])

    def test_wait_for_boot_keeps_every_event_returned_by_the_same_poll(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory));clock=live.clock
            self.feed(live,BANNER+BOOT+b'M5 ARM token=31 at=0\r\n')
            self.assertEqual(live.wait_for_boot(deadline=clock()+5,sleep=clock.sleep)['kind'],'boot')
            self.assertEqual([e['kind'] for e in live.poll(0)],['boot','target'])

    def test_operator_file_is_not_a_channel_in_the_focused_adapter(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            (live.directory/'operator.jsonl').write_text(json.dumps(dict(event='cable_disconnect_receipt',monotonic=1.))+'\n')
            with self.assertRaises(ValueError):live.poll(0)

    def test_close_is_idempotent_and_closes_the_port(self):
        with tempfile.TemporaryDirectory() as directory:
            live,port,link=self.make(Path(directory))
            live.close();live.close()
            self.assertTrue(port.closed)


class Replay(unittest.TestCase):
    def write(self,root,raw,chunks,prefix):
        (root/'uart-rx.bin').write_bytes(raw)
        (root/'receive.jsonl').write_text(''.join(json.dumps(dict(monotonic=1.+i/1000,hex=c.hex()))+'\n' for i,c in enumerate(chunks)))
        (root/'focused-prefix.json').write_text(json.dumps(prefix))

    def test_non_ascii_junk_before_the_banner_replays_like_it_runs_live(self):
        junk=b'\xff\x00\xfe\r\n\x80'
        raw=junk+BANNER+BOOT+b'M5 ARM token=31 at=0\r\n'
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.write(root,raw,[raw[:3],raw[3:30],raw[30:]],runner.prefix_record(junk))
            rows=runner.uart_records(root,True)
            self.assertEqual([r['record']['event'] for r in rows],['BOOT','ARM'])
            self.assertEqual(rows[0]['monotonic'],1.002)
            live=focused.FocusedAdapter(31,root/'live',Port,receiver_factory=Receiver,process_backend=Backend(),link=FakeLink(),clock=Time(),software_only=True)
            (live.directory/'receive.jsonl').write_text(''.join(json.dumps(dict(monotonic=1.+i/1000,hex=c.hex()))+'\n' for i,c in enumerate([raw[:3],raw[3:30],raw[30:]])))
            self.assertEqual([e['kind'] for e in live.poll(0)],['boot','target'])

    def test_a_planted_prefix_file_does_not_switch_a_full_replay_to_the_focused_path(self):
        junk=b'stale M3 bytes '
        raw=junk+BANNER+BOOT
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.write(root,raw,[raw],runner.prefix_record(junk))
            with self.assertRaises(ValueError):runner.uart_records(root)

    def test_replay_cuts_the_retained_prefix_and_rejects_tampering(self):
        junk=b'stale M3 bytes '
        raw=junk+BANNER+BOOT+b'M5 ARM token=31 at=0\r\n'
        prefix=dict(raw_start=0,raw_end=len(junk),sha256=hashlib.sha256(junk).hexdigest(),reason='pre-banner bytes retained, not parsed')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.write(root,raw,[raw[:20],raw[20:]],prefix)
            rows=runner.uart_records(root,True)
            self.assertEqual([r['record']['event'] for r in rows],['BOOT','ARM'])
            bad=dict(prefix,sha256='0'*64);self.write(root,raw,[raw[:20],raw[20:]],bad)
            with self.assertRaises(ValueError):runner.uart_records(root,True)
            self.write(root,raw,[raw[:20],raw[20:]],dict(prefix,raw_end=len(junk)+1,sha256=hashlib.sha256(raw[:len(junk)+1]).hexdigest()))
            with self.assertRaises(ValueError):runner.uart_records(root,True)
            self.write(root,raw.replace(b'stale',b'M5 ok'),[raw[:20],raw[20:]],prefix)
            with self.assertRaises(ValueError):runner.uart_records(root,True)


class RawPort(unittest.TestCase):
    def test_bounded_read_and_write_over_a_pty(self):
        import pty
        master,slave=pty.openpty();path=os.ttyname(slave)
        try:
            port=focused.RawPort(path)
            try:
                self.assertEqual(port.read(16),b'')
                os.write(master,b'hello\r\n')
                data=b''
                for _ in range(10):
                    data+=port.read(16)
                    if b'\n' in data:break
                self.assertIn(b'hello',data)
                self.assertEqual(port.write(b'A 1\n'),4)
                self.assertTrue(0<=port.timeout<=.02 and 0<port.write_timeout<=.25)
            finally:port.close()
        finally:
            os.close(master);os.close(slave)


class Capture(unittest.TestCase):
    ABI={'ETIMEDOUT':116,'ECONNRESET':104,'ENETDOWN':115,'ENETUNREACH':114}

    def test_missing_sudo_credential_stops_before_the_uart_is_opened(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);time=Time();host=Host(time,sudo_rc=1)
            link=focused.HostLink(run=host,clock=time,sleep=time.sleep);opened=[]
            with self.assertRaises(ValueError):
                focused_cli.capture(31,'123456789abc',self.ABI,root/'evidence','/dev/null',1,link=link,port_factory=lambda:opened.append(1),clock=time,sleep=time.sleep,say=lambda _:None)
            self.assertEqual(opened,[]);self.assertFalse((root/'evidence').exists())

    def test_no_boot_before_the_operator_deadline_is_inconclusive_and_changes_no_link(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);time=Time();host=Host(time)
            link=focused.HostLink(run=host,clock=time,sleep=time.sleep)
            with self.assertRaises(ValueError):
                focused_cli.capture(31,'123456789abc',self.ABI,root/'evidence','/dev/null',.05,link=link,port_factory=Port,clock=time,sleep=time.sleep,say=lambda _:None,receiver_factory=Receiver,process_backend=Backend(),software_only=True)
            result=json.loads((root/'evidence'/'result.jsonl').read_text().splitlines()[-1])
            self.assertEqual((result['status'],result['automatic_reset']),('ABORT_INCONCLUSIVE',False))
            self.assertEqual([c for c in host.calls if c[0]=='sudo' and c[-1] in ('up','down')],[])

    def test_sudo_credential_is_rechecked_after_the_boot_banner_and_before_arm(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);time=Time();host=Host(time)
            link=focused.HostLink(run=host,clock=time,sleep=time.sleep)
            raw=BANNER+BOOT
            class Armed(focused.FocusedAdapter):
                def __init__(self,*args,**kwargs):
                    super().__init__(*args,**kwargs)
                    with (self.directory/'receive.jsonl').open('a') as out:out.write(json.dumps(dict(monotonic=1.,hex=raw.hex()))+'\n')
                    (self.directory/'uart-rx.bin').write_bytes(raw)
            def expire(argv,**kwargs):
                if argv==['sudo','-n','true'] and any(a==['sudo','-n','true'] for a in host.calls):
                    result=host(argv,**kwargs);result.returncode=1;return result
                return host(argv,**kwargs)
            link=focused.HostLink(run=expire,clock=time,sleep=time.sleep);written=[]
            def run(supervisor,adapter,clock):written.append(1)
            with self.assertRaises(ValueError):
                focused_cli.capture(31,'123456789abc',self.ABI,root/'evidence','/dev/null',5,link=link,port_factory=Port,clock=time,sleep=time.sleep,say=lambda _:None,receiver_factory=Receiver,process_backend=Backend(),software_only=True,adapter_cls=Armed,run=run)
            self.assertEqual(written,[])

    def test_termination_signals_become_exits_so_cleanup_restores_the_nic(self):
        import signal
        saved={n:signal.getsignal(n) for n in (signal.SIGTERM,signal.SIGHUP)}
        try:
            focused_cli.install_signal_handlers()
            for number in saved:
                with self.assertRaises(SystemExit):signal.getsignal(number)(number,None)
        finally:
            for number,handler in saved.items():signal.signal(number,handler)

    def test_abort_with_the_host_link_down_restores_it_and_keeps_the_evidence_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);time=Time();host=Host(time)
            link=focused.HostLink(run=host,clock=time,sleep=time.sleep)
            def run(supervisor,adapter,clock):
                adapter.notify_operator(dict(event='cable_disconnect_cue',monotonic=clock()))
                raise ValueError('stop')
            raw=BANNER+BOOT
            def factory():
                return Port()
            class Armed(focused.FocusedAdapter):
                def __init__(self,*args,**kwargs):
                    super().__init__(*args,**kwargs)
                    with (self.directory/'receive.jsonl').open('a') as out:out.write(json.dumps(dict(monotonic=1.,hex=raw.hex()))+'\n')
                    (self.directory/'uart-rx.bin').write_bytes(raw)
            with self.assertRaises(ValueError):
                focused_cli.capture(31,'123456789abc',self.ABI,root/'evidence','/dev/null',5,link=link,port_factory=factory,clock=time,sleep=time.sleep,say=lambda _:None,receiver_factory=Receiver,process_backend=Backend(),software_only=True,adapter_cls=Armed,run=run)
            self.assertEqual([c[-1] for c in host.calls if c[0]=='sudo' and c[-1] in ('up','down')],['down','up'])
            self.assertTrue((root/'evidence'/'abort-restore.jsonl').exists())
            controls=[json.loads(l)['event'] for l in (root/'evidence'/'controls.jsonl').read_text().splitlines()]
            self.assertEqual(controls,['cable_disconnect_cue','host_down','cable_disconnect_receipt'])


class FocusedBundle(unittest.TestCase):
    """Retained raw bundle, including a junk pre-banner prefix, replays to PASS or fails shut."""
    def build(self,root,tamper=False):
        from test_m5_series import focused,ABI,SOURCE
        data=focused();boot=data['boots'][0];folder=root
        raw=b'';receive=[];junk=b'stale bytes '
        first=True
        for row in boot['uart']:
            record=row['record']
            text='M5_BOOT source='+record['source'] if record['event']=='BOOT' else 'M5 '+record['event']+' '+' '.join(str(k)+'='+str(v) for k,v in record.items() if k!='event')
            chunk=(text+'\r\n').encode('ascii')
            if first:
                chunk=junk+BANNER+chunk;first=False
            raw+=chunk;receive.append(dict(monotonic=row['monotonic'],hex=chunk.hex()))
        (folder/'uart-rx.bin').write_bytes(raw)
        (folder/'receive.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in receive))
        (folder/'focused-prefix.json').write_text(json.dumps(runner.prefix_record(junk)))
        start=boot['uart'][0]['monotonic']-.01;end=boot['uart'][-1]['monotonic']+.1;polls=[]
        while start<end:polls.append(dict(monotonic=start,gap=.1));start+=.1
        polls.append(dict(monotonic=end,gap=.1))
        (folder/'poll.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in polls))
        def jsonl(name,rows):
            (folder/name).write_text(''.join(json.dumps(r)+'\n' for r in rows));return name
        peers={phase:jsonl('peer-'+phase+'.jsonl',rows) for phase,rows in boot['peers'].items()}
        reset=dict(operation='focused_power_cycle_1',monotonic=boot['uart'][0]['monotonic']-.001)
        index=dict(schema=1,source=SOURCE,clock=data['clock'],reset_semantics=focused_cli.RESET_WITNESS,boots=[dict(nonce=boot['nonce'],reset=reset,uart_directory='.',writes_file=jsonl('writes.jsonl',boot['writes']),controls_file=jsonl('controls.jsonl',boot['controls']),peer_stops_file=jsonl('peer-stops.jsonl',boot['peer_stops']),peer_files=peers)])
        (folder/'index.json').write_text(json.dumps(index))
        return folder/'index.json',SOURCE,ABI

    def test_focused_bundle_passes_and_tampering_fails_shut(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);index,source,abi=self.build(root)
            result=runner.replay_bundle(index,source,abi,runner.m5_series.FOCUSED)
            self.assertEqual((result['status'],result['profile'],result['boots'],result['calls']),('PASS','focused',1,6))
            self.assertEqual(result['reset_witness'],'operator power cycle, not host-timestamped')
            data=json.loads(index.read_text());data['reset_semantics']='host reset';index.write_text(json.dumps(data))
            with self.assertRaises(ValueError):runner.replay_bundle(index,source,abi,runner.m5_series.FOCUSED)
            data['reset_semantics']=focused_cli.RESET_WITNESS;index.write_text(json.dumps(data))
            with self.assertRaises(ValueError):runner.replay_bundle(index,source,abi)
            (root/'uart-rx.bin').write_bytes((root/'uart-rx.bin').read_bytes()+b'x')
            with self.assertRaises(ValueError):runner.replay_bundle(index,source,abi,runner.m5_series.FOCUSED)


if __name__=='__main__':unittest.main()

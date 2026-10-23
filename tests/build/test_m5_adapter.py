"""Operational adapter uses injected transports; no hardware access in tests."""
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import capture_m5_network as runner
import m5_capture_adapter as adapter

class Clock:
    value=1.
    def __call__(self):return self.value

class Port:
    def __init__(self):self.writes=[];self.closed=False;self.write_timeout=.25;self.timeout=.02
    def write(self,data):self.writes.append(data);return len(data)
    def close(self):self.closed=True

class Receiver:
    failure=None
    def __init__(self,port,directory):self.directory=directory
    def begin(self):
        for name in ('uart-rx.bin','receive.jsonl','poll.jsonl'):(self.directory/name).write_bytes(b'')
    def finish(self):pass

class FakeProcess:
    stopped=False
    def poll(self):return 0 if self.stopped else None
    def send_signal(self,_):self.stopped=True
    def wait(self,timeout):return 0

class Backend:
    def __init__(self):self.process=None
    def start_peer(self,nonce,phase,count,mode,output,log):
        self.process=FakeProcess();output.write_text(json.dumps(dict(event='ready',clock=runner.m5_series.CLOCK,monotonic=1.,token=nonce,phase=phase,mode=mode,address='192.168.77.1',tcp_port=42420,udp_port=42421))+'\n');return self.process
    def stop_peer(self,process):process.send_signal(15);return process.wait(3)

class FakeNIC:
    def __init__(self,*_):self.closed=False;self.expected=None
    def start(self,route=False):self.expected='route' if route else 'host_down'
    def poll(self):return None
    def close(self):self.closed=True

class Adapter(unittest.TestCase):
    def make(self,root,approved=True):
        prepared=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64);approval=runner.approval_template(prepared)
        if approved:
            approval['authorization']='APPROVED_FOR_PHYSICAL_EXECUTION';approval['operations']={key:True for key in approval['operations']}
        port=Port();calls=[]
        def factory():calls.append('open');return port
        return prepared,approval,port,calls,factory
    def test_approval_failure_precedes_device_factory(self):
        with tempfile.TemporaryDirectory() as directory:
            p,a,port,calls,factory=self.make(Path(directory),False)
            with self.assertRaises(ValueError):adapter.CaptureAdapter(p,a,1,Path(directory)/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True)
            self.assertEqual(calls,[])
    def test_exact_raw_receive_uart_write_and_peer_reap(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root)
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True,process_backend=Backend(),clock=Clock())
            raw=b'M5_BOOT source=123456789abc\r\nM5 ARM token=31 at=0\n'
            (live.directory/'uart-rx.bin').write_bytes(raw)
            (live.directory/'receive.jsonl').write_text(json.dumps(dict(monotonic=1.,hex=raw.hex()))+'\n')
            events=live.poll(0)
            self.assertEqual([e['kind'] for e in events],['boot','target'])
            live.write_uart('A 31\n',deadline=2.);self.assertEqual(port.writes,[b'A 31\n'])
            live.prepare_peer(31,2,10,'echo');self.assertEqual(live.poll(0)[0]['kind'],'peer_ready')
            self.assertEqual(live.stop_peer(),dict(phase=2,reaped=True))
            self.assertTrue((live.directory/'peer-stops.jsonl').exists())
            live.close();self.assertTrue(port.closed)
    def test_invalid_uart_bytes_or_short_write_abort(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root)
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True,process_backend=Backend(),clock=Clock())
            (live.directory/'receive.jsonl').write_text(json.dumps(dict(monotonic=1.,hex=b'M5_FAIL stopped=1\n'.hex()))+'\n')
            with self.assertRaises(ValueError):live.poll(0)
            live.close()

class Failures(unittest.TestCase):
    make=Adapter.make
    def test_tampered_manifest_or_stale_guard_precedes_uart_open(self):
        for fail in ('manifest','guard'):
            with self.subTest(fail=fail),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);p,a,port,calls,factory=self.make(root)
                def reject(*_):raise ValueError('Unverified '+fail)
                if fail=='guard':factory.verify=reject
                with self.assertRaises(ValueError):adapter.CaptureAdapter(p,a,1,root/'boot',factory,manifest_check=reject if fail=='manifest' else lambda:None,receiver_factory=Receiver,nic_factory=FakeNIC)
                self.assertEqual(calls,[])
    def test_startup_failure_and_peer_stop_failure_release_all_owned_resources(self):
        class BadReceiver(Receiver):
            finished=False
            def begin(self):raise ValueError('Reader init failed')
            def finish(self):self.finished=True
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root)
            with self.assertRaises(ValueError):adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=BadReceiver,nic_factory=FakeNIC,software_only=True)
            self.assertTrue(port.closed)
        class BadBackend(Backend):
            def stop_peer(self,process):raise ValueError('Peer unreaped')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root)
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,process_backend=BadBackend(),software_only=True)
            live.prepare_peer(31,2,10,'echo')
            with self.assertRaises(ValueError):live.close()
            self.assertTrue(port.closed);self.assertTrue(live.nic.closed)
            self.assertEqual(port.writes,[])
    def test_slow_guard_cannot_write_after_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root);clock=Clock()
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True,clock=clock)
            def slow(*_):clock.value=3.
            live.safety=slow
            with self.assertRaises(ValueError):live.write_uart('A 31\n',deadline=2.)
            self.assertEqual(port.writes,[]);live.close()
    def test_final_partial_peer_line_is_drained_after_reap(self):
        class CompletingBackend(Backend):
            def start_peer(self,*args):
                process=super().start_peer(*args);self.output=args[-2];return process
            def stop_peer(self,process):
                with self.output.open('a') as output:output.write('"exchange"}\n')
                return super().stop_peer(process)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=self.make(root)
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,process_backend=CompletingBackend(),software_only=True,clock=Clock())
            live.prepare_peer(31,2,10,'echo');live.poll(0)
            with live.peer_path.open('a') as output:output.write('{"event":')
            live.poll(0)
            self.assertEqual(live.stop_peer(),dict(phase=2,reaped=True));live.close()

class Carrier(unittest.TestCase):
    def test_exact_carrier_status_only(self):
        from unittest.mock import patch
        prefix='ether 00:0a:cd:41:89:cc\ninet 192.168.77.1 netmask 0xffffff00\n'
        for status,event in [('status: active\n','host_up'),('status: inactive\n','host_down'),('',None),('status: unknown\n',None),('status: active\nstatus: inactive\n',None)]:
            with self.subTest(status=status),tempfile.TemporaryDirectory() as directory:
                class Process:
                    def poll(self):return 0
                    def wait(self,timeout):return 0
                def start(*args,**kwargs):kwargs['stdout'].write((prefix+status).encode());kwargs['stdout'].flush();return Process()
                with patch.object(adapter.subprocess,'Popen',start):
                    probe=adapter.NICProbe(Path(directory),Clock());probe.start()
                    if event:self.assertEqual(probe.poll()['event'],event)
                    else:
                        with self.assertRaises(ValueError):probe.poll()

class DirectRoute(unittest.TestCase):
    # Synthetic examples of Apple route.c print_getmsg output; never query a NIC.
    NETWORK='   route to: 192.168.77.2\ndestination: 192.168.77.0\n       mask: 255.255.255.0\n  interface: en5\n      flags: <UP,DONE,CLONING,STATIC,IFSCOPE>\n'
    HOST='   route to: 192.168.77.2\ndestination: 192.168.77.2\n  interface: en5\n      flags: <UP,HOST,DONE,LLINFO,WASCLONED,IFSCOPE,IFREF>\n'
    def probe(self,text,source='192.168.77.1'):
        from unittest.mock import patch
        outputs=[f'ether 00:0a:cd:41:89:cc\ninet {source} netmask 0xffffff00\nstatus: active\n',text]
        class Process:
            def poll(self):return 0
            def wait(self,timeout):return 0
        def start(*args,**kwargs):
            kwargs['stdout'].write(outputs.pop(0).encode());kwargs['stdout'].flush();return Process()
        with tempfile.TemporaryDirectory() as directory,patch.object(adapter.subprocess,'Popen',start):
            probe=adapter.NICProbe(Path(directory),Clock());probe.start();probe.poll()
            probe.start(route=True);return probe.poll()
    def test_network_and_cloned_host_are_direct_routes(self):
        for text in (self.NETWORK,self.HOST):
            with self.subTest(text=text):
                row=self.probe(text)
                self.assertEqual((row['source'],row['destination']),('192.168.77.1','192.168.77.2'))
    def test_missing_inactive_and_stale_source_observations_are_rejected(self):
        from unittest.mock import patch
        for observed in (None,dict(source='192.168.77.1',active=False,at=1.),
                         dict(source='192.168.77.1',active=True,at=-2.)):
            with self.subTest(observed=observed),tempfile.TemporaryDirectory() as directory:
                class Process:
                    def poll(self):return 0
                    def wait(self,timeout):return 0
                def start(*args,**kwargs):
                    kwargs['stdout'].write(self.NETWORK.encode());kwargs['stdout'].flush();return Process()
                with patch.object(adapter.subprocess,'Popen',start):
                    probe=adapter.NICProbe(Path(directory),Clock());probe.interface_observation=observed
                    probe.start(route=True)
                    with self.assertRaises(ValueError):probe.poll()
    def test_gateway_and_incomplete_or_unexpected_routes_are_rejected(self):
        cases=[self.NETWORK+'    gateway: 192.168.77.254\n',
               self.NETWORK.replace('<UP,','<UP,GATEWAY,'),
               self.NETWORK.replace('<UP,','<UP,REJECT,'),
               self.NETWORK.replace('<UP,','<UP,BLACKHOLE,'),
               self.NETWORK.replace('192.168.77.0','0.0.0.0'),
               self.NETWORK.replace('255.255.255.0','255.255.0.0'),
               self.NETWORK.replace('route to: 192.168.77.2','route to: 192.168.77.3'),
               self.NETWORK.replace('interface: en5','interface: en0'),
               'interface: en5\n',self.NETWORK+'interface: en5\n',
               self.NETWORK.replace('<UP,','<'),
               self.HOST.replace('192.168.77.2\n  interface','192.168.77.3\n  interface')]
        for text in cases:
            with self.subTest(text=text),self.assertRaises(ValueError):self.probe(text)
        with self.assertRaises(ValueError):self.probe(self.NETWORK,source='192.168.77.9')

class EntryGates(unittest.TestCase):
    def test_capture_without_execute_flag_is_inert(self):
        from types import SimpleNamespace
        with self.assertRaises(ValueError):runner.capture_approved_boot(SimpleNamespace(execute_hardware=False))
    def test_nic_timeout_is_not_a_down_witness(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            class Process:
                killed=False
                def poll(self):return None
                def kill(self):self.killed=True
                def wait(self,timeout):return 0
            process=Process();clock=Clock()
            with patch.object(adapter.subprocess,'Popen',return_value=process):
                probe=adapter.NICProbe(Path(directory),clock);probe.start();clock.value=3.
                with self.assertRaises(ValueError):probe.poll()
                self.assertTrue(process.killed)

class Reset(unittest.TestCase):
    def test_explicit_same_owner_reset_after_receiver_and_no_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=Adapter.make(self,root)
            def reset(boot,output,**_):
                self.assertTrue((root/'boot'/'receive.jsonl').exists());calls.append('reset'+str(boot));output.write_text('software fake reset')
            factory.reset=reset
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True)
            live.perform_approved_reset(deadline=200.)
            with self.assertRaises(ValueError):live.perform_approved_reset(deadline=200.)
            self.assertEqual(calls,['open','reset1']);self.assertEqual(port.writes,[]);live.close()

class LeaseCleanup(unittest.TestCase):
    def test_ioctl_failure_after_serial_open_releases_port_and_lock(self):
        from types import ModuleType,SimpleNamespace
        from unittest.mock import patch
        import fcntl
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);safety=root/'safety';safety.mkdir();port=Port();port.fileno=lambda:42
            lease=adapter.M3UARTLease({},safety);lease.verify=lambda *_:None
            serial=ModuleType('serial');serial.__path__=[];serial.Serial=lambda *_,**__:port
            tools=ModuleType('serial.tools');tools.__path__=[]
            ports=ModuleType('serial.tools.list_ports');ports.comports=lambda:[SimpleNamespace(device='/dev/cu.usbmodem11302',serial_number=runner.UART_USB,vid=0x0d28,pid=0x0204)]
            serial.tools=tools;tools.list_ports=ports
            p,a,_,_,_=Adapter.make(self,root)
            with patch.dict(sys.modules,{'serial':serial,'serial.tools':tools,'serial.tools.list_ports':ports}),patch.object(fcntl,'ioctl',side_effect=OSError('Software injected ioctl failure')):
                with self.assertRaises(OSError):adapter.CaptureAdapter(p,a,1,root/'boot',lease,manifest_check=lambda:None,receiver_factory=Receiver,nic_factory=FakeNIC)
            self.assertTrue(port.closed);self.assertIsNone(lease.lock)

class ResetReserve(unittest.TestCase):
    def test_slow_guard_never_starts_reset_after_reserve_expires(self):
        from unittest.mock import patch
        import capture_m3
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'m5.bin';binary.write_bytes(b'software fixture')
            lease=adapter.M3UARTLease({},Path(directory),image_binary=binary,image_sha256=runner.sha(binary));clock=Clock()
            def slow(*_):clock.value=500.
            lease.verify=slow
            with patch.object(capture_m3,'commander') as commander:
                with self.assertRaises(ValueError):lease.reset(1,Path(directory)/'reset.log',deadline=530.,clock=clock)
                commander.assert_not_called()
    def test_accepted_identity_checked_after_exact_same_owner_reset(self):
        from unittest.mock import patch
        import capture_m3
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'m5.bin';binary.write_bytes(b'software fixture')
            lease=adapter.M3UARTLease({},Path(directory),image_binary=binary,image_sha256=runner.sha(binary));lease.verify=lambda *_:None
            text='CPUID register: 0x411FC271\nDPIDR: 0x0BD11477\nS/N: 602003106\nCortex-M7\nVTref = 3.300 V\n'
            with patch.object(capture_m3,'commander',side_effect=[text,text+'Verify successful',text]) as commander:
                lease.reset(1,Path(directory)/'reset.log',deadline=500.,clock=Clock())
                self.assertEqual([call.args[1] for call in commander.call_args_list],['q\n','verifybin "'+str(binary.resolve())+'", 0x60000000\nq\n','r\ng\nq\n'])
            with patch.object(capture_m3,'commander',return_value=text.replace('3.300','1.000')):
                with self.assertRaises(ValueError):lease.reset(1,Path(directory)/'reset.log',deadline=500.,clock=Clock())

class OperatorReceipt(unittest.TestCase):
    def test_exact_shared_clock_receipt_and_negative_missing_confirmation_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'operator.jsonl').write_text('')
            (root/'adapter.jsonl').write_text(json.dumps(dict(event='capture_started',boot=1,nonce=31))+'\n')
            with self.assertRaises(ValueError):adapter.record_operator_receipt(root,'cable_disconnect_receipt',clock=Clock())
            adapter.record_operator_receipt(root,'cable_disconnect_receipt',confirmed=True,clock=Clock())
            row=json.loads((root/'operator.jsonl').read_text());self.assertEqual(row,dict(event='cable_disconnect_receipt',monotonic=1.))
            with self.assertRaises(ValueError):adapter.record_operator_receipt(root,'cable_disconnect_receipt',confirmed=True,clock=Clock())

class Preflight(unittest.TestCase):
    def test_identity_voltage_and_current_image_verified_before_any_reset(self):
        from unittest.mock import patch
        import capture_m3
        good='CPUID register: 0x411FC271\nDPIDR: 0x0BD11477\nS/N: 602003106\nCortex-M7\nVTref = 3.300 V\n'
        for responses,want_calls in [([good.replace('3.300','1.000')],1),([good.replace('602003106','000000000')],1),([good,good+'Verify failed'],2),([good,good+'Verify successful',good],3)]:
            with self.subTest(responses=responses),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);binary=root/'m5.bin';binary.write_bytes(b'software fixture only')
                lease=adapter.M3UARTLease({},root,image_binary=binary,image_sha256=runner.sha(binary));lease.verify=lambda *_:None
                with patch.object(capture_m3,'commander',side_effect=responses) as commander:
                    if want_calls==3:lease.reset(1,root/'reset.log',deadline=500.,clock=Clock())
                    else:
                        with self.assertRaises(ValueError):lease.reset(1,root/'reset.log',deadline=500.,clock=Clock())
                    self.assertEqual(commander.call_count,want_calls)
                    self.assertEqual(commander.call_args_list[0].args[1],'q\n')
                    if want_calls>=2:self.assertEqual(commander.call_args_list[1].args[1],'verifybin "'+str(binary.resolve())+'", 0x60000000\nq\n')
                    if want_calls==3:self.assertEqual(commander.call_args_list[2].args[1],'r\ng\nq\n')

class ResetPrefix(unittest.TestCase):
    def test_old_passive_failure_is_retained_and_only_fresh_boot_is_qualified(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=Adapter.make(self,root);clock=Clock()
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True,clock=clock)
            def append(raw,stamp):
                with (live.directory/'uart-rx.bin').open('ab') as output:output.write(raw)
                with (live.directory/'receive.jsonl').open('a') as output:output.write(json.dumps(dict(monotonic=stamp,hex=raw.hex()))+'\n')
            old=b'M5_FAIL stopped=1 no_more_traffic=1\r\n';append(old,.5)
            def reset(boot,output,**options):
                options['record']('approved_preflight_completed');options['record']('approved_reset_started')
                clock.value=2.;append(b'*** Booting Zephyr OS build 4.2.0 ***\r\nM5_BOOT source=123456789abc\r\n',2.)
                options['record']('approved_reset_completed')
            factory.reset=reset;live.perform_approved_reset(deadline=200.)
            self.assertEqual([row['kind'] for row in live.poll(0)],['boot'])
            self.assertEqual(runner.uart_records(live.directory),[dict(monotonic=2.,record=dict(event='BOOT',source='123456789abc'))])
            self.assertTrue((live.directory/'uart-rx.bin').read_bytes().startswith(old))
            boundary=json.loads((live.directory/'prefix-boundary.json').read_text());self.assertEqual(boundary['raw_end'],len(old))
            append(b'M5_FAIL stopped=1 no_more_traffic=1\n',3.)
            with self.assertRaises(ValueError):live.poll(0)
            with self.assertRaises(ValueError):runner.uart_records(live.directory)
            live.close()
    def test_unknown_or_active_prefix_blocks_reset_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p,a,port,calls,factory=Adapter.make(self,root)
            live=adapter.CaptureAdapter(p,a,1,root/'boot',factory,receiver_factory=Receiver,nic_factory=FakeNIC,software_only=True,clock=Clock())
            raw=b'UNKNOWN TARGET OUTPUT\n';(live.directory/'uart-rx.bin').write_bytes(raw)
            (live.directory/'receive.jsonl').write_text(json.dumps(dict(monotonic=.5,hex=raw.hex()))+'\n')
            with self.assertRaises(ValueError):live.mark_reset_boundary()
            self.assertFalse((live.directory/'reset-boundary.json').exists());self.assertEqual(port.writes,[]);live.close()

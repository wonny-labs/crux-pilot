"""Preparation runner stays inert and binds every proposed operation."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import capture_m5_network as runner

class Runner(unittest.TestCase):
    def test_exact_operation_plan_and_single_use_tickets(self):
        plan=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64)
        self.assertEqual(len(plan['boots']),3)
        self.assertEqual(plan['boots'][0]['commands'][1]['template'],'P 31 2 10 <fresh READY ticket>')
        self.assertEqual(plan['boots'][0]['commands'][6]['template'],'L 31 7 <fresh READY ticket>')
        self.assertEqual(plan['status'],'SOFTWARE_PREPARED_HARDWARE_NOT_AUTHORIZED')
        self.assertIn('program_and_start',plan['operations'])
        self.assertIn('verify_entry_image',plan['operations'])
        self.assertEqual(runner.command(31,2,10,99),'P 31 2 10 99\n')
        for values in [(0,2,10,99),(31,2,10,0),(31,2,1,99),(31,10,1,99)]:
            with self.assertRaises(ValueError):runner.command(*values)
    def test_approval_is_exact_and_all_operations_individually_explicit(self):
        plan=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64);approval=runner.approval_template(plan)
        with self.assertRaises(ValueError):runner.validate_approval(approval,plan)
        approval['authorization']='APPROVED_FOR_PHYSICAL_EXECUTION'
        for operation in approval['operations']:approval['operations'][operation]=True
        runner.validate_approval(approval,plan)
        for mutate in [lambda a:a.update(manifest_sha256='b'*64),lambda a:a['operations'].update(program_and_start=False),lambda a:a['operations'].update(unapproved_retry=True),lambda a:a.update(nonces=[31,32,34]),lambda a:a.update(probe_serial='other')]:
            data=json.loads(json.dumps(approval));mutate(data)
            with self.assertRaises(ValueError):runner.validate_approval(data,plan)
    def test_approval_binds_complete_safety_procedure_and_evidence(self):
        import copy
        plan=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64)
        approval=runner.approval_template(plan)
        approval['authorization']='APPROVED_FOR_PHYSICAL_EXECUTION'
        approval['operations']={key:True for key in approval['operations']}
        for field in ('procedure','evidence'):
            changed=copy.deepcopy(plan)
            if field=='procedure':changed[field][0]='Skip the root guard'
            else:changed[field]['raw']='Discard original bytes'
            with self.subTest(field=field),self.assertRaises(ValueError):
                runner.validate_approval(approval,changed)

    def test_approval_contains_hash_of_whole_plan(self):
        plan=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64)
        approval=runner.approval_template(plan)
        self.assertIn('plan_sha256',approval)
        self.assertEqual(approval['plan_sha256'],hashlib.sha256(json.dumps(plan,sort_keys=True,separators=(',',':')).encode()).hexdigest())

    def test_no_execute_path_and_no_device_imports(self):
        path=Path(runner.__file__)
        output=subprocess.run([sys.executable,str(path),'--execute-hardware'],capture_output=True,text=True)
        self.assertNotEqual(output.returncode,0)
        self.assertNotIn('serial',sys.modules)
    def test_raw_uart_replay_detects_missing_and_modified_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);raw=b'M5_BOOT source=123456789abc\r\nM5 ARM token=31 at=0\n'
            (root/'uart-rx.bin').write_bytes(raw)
            (root/'receive.jsonl').write_text(json.dumps(dict(monotonic=1.,hex=raw.hex()))+'\n')
            rows=runner.uart_records(root)
            self.assertEqual(rows[0]['record'],dict(event='BOOT',source='123456789abc'))
            self.assertEqual(rows[1]['record']['token'],31)
            (root/'uart-rx.bin').write_bytes(raw+b'x')
            with self.assertRaises(ValueError):runner.uart_records(root)


class Supervisor(unittest.TestCase):
    def ready(self):
        s=runner.Supervisor(31,'123456789abc',0,ABI={'ETIMEDOUT':116,'ECONNRESET':104,'ENETDOWN':115,'ENETUNREACH':114})
        self.assertEqual(s.boot('123456789abc',.1),'A 31\n')
        s.target(dict(event='ARM',token=31,at=0),.2)
        s.target(dict(event='READY',token=31,seq=1,ticket=99,at=0),.3)
        return s
    def test_tickets_consumed_and_no_queue_or_peerless_command(self):
        s=self.ready();self.assertEqual(s.next_command(.4),'I 31 1 99\n')
        with self.assertRaises(ValueError):s.next_command(.5)
        self.assertTrue(s.aborted)
        with self.assertRaises(ValueError):s.next_command(.6)
    def test_idle_completion_advances_and_peer_ready_is_required(self):
        s=self.ready();s.next_command(.4)
        for at in (0,1000,2000):s.target(dict(event='IDLE',token=31,seq=1,checkpoint=0,at=at,contexts=0,rx_pkt=16,tx_pkt=16,rx_buf=32,tx_buf=32,main_free=1500,observer_free=800),.5+at/1000)
        s.target(dict(event='DONE',token=31,seq=1,at=2001),2.6)
        s.target(dict(event='READY',token=31,seq=2,ticket=100,at=2001),2.7)
        with self.assertRaises(ValueError):s.next_command(2.8)
        s=self.ready();s.next_command(.4)
        with self.assertRaises(ValueError):s.tick(10.4)
        self.assertTrue(s.aborted)
    def test_unconfirmed_loss_cannot_cue_disconnect(self):
        s=self.ready()
        with self.assertRaises(ValueError):s.disconnect_cue(.5)
        self.assertTrue(s.aborted)

    def test_complete_series_boot_drives_transport_independent_core(self):
        from test_m5_series import complete,ABI,SOURCE
        boot=complete()['boots'][0]
        request=next(r for r in boot['peers']['7'] if r['event']=='request')
        begin=next(r for r in boot['uart'] if r['record']['event']=='BEGIN' and r['record']['seq']==7)
        begin['monotonic']=request['monotonic']+.0005
        s=runner.Supervisor(boot['nonce'],SOURCE,0,ABI)
        timeline=[]
        timeline += [(e['monotonic'],'target',e['record']) for e in boot['uart']]
        timeline += [(e['monotonic'],'write',e) for e in boot['writes']]
        timeline += [(e['monotonic'],'peer',e) for rows in boot['peers'].values() for e in rows if e['event'] in ('ready','request')]
        timeline += [(e['monotonic'],'control',e) for e in boot['controls']]
        arm=None
        for now,kind,row in sorted(timeline,key=lambda e:e[0]):
            if kind=='target':
                if row['event']=='BOOT':arm=s.boot(row['source'],now)
                else:s.target(row,now)
            elif kind=='write':
                if row['line'].startswith('A '):self.assertEqual(row['line'],arm)
                else:self.assertEqual(row['line'],s.next_command(now))
            elif kind=='peer':
                if row['event']=='ready':
                    if s.peer_phase is not None and s.stopped_phase!=s.peer_phase:s.peer_stopped(s.peer_phase,True,now)
                    s.peer_ready(row,now)
                elif row['phase']==7:s.peer_request(row,now)
            elif row['event']=='cable_disconnect_cue':self.assertEqual(row,s.disconnect_cue(now))
            else:
                if row['event']=='cable_connect_receipt' and s.stopped_phase!=7:s.peer_stopped(7,True,now)
                s.control(row,now)
        self.assertEqual(s.state,'finished')
        self.assertFalse(s.aborted)

class Loop(unittest.TestCase):
    def test_bounded_loop_aborts_without_reset_or_extra_command(self):
        class Clock:
            now=0
            def __call__(self):return self.now
        class Adapter:
            def __init__(self,clock):self.clock=clock;self.writes=[];self.stopped=0
            def poll(self,timeout):
                if timeout>.25:raise AssertionError('Unbounded poll')
                self.clock.now+=timeout
                if self.clock.now==.25:return [dict(kind='boot',monotonic=.25,record=dict(source='123456789abc'))]
                return []
            def write_uart(self,line,*,deadline):self.writes.append(line);return self.clock() if callable(self.clock) else self.clock.now
            def stop_peer(self):self.stopped+=1
            def prepare_peer(self,*_):raise AssertionError('No peer before READY')
        clock=Clock();adapter=Adapter(clock)
        s=runner.Supervisor(31,'123456789abc',0,{})
        with self.assertRaises(ValueError):runner.run_boot(s,adapter,clock)
        self.assertEqual(adapter.writes,['A 31\n'])
        self.assertEqual(adapter.stopped,1)
        self.assertTrue(s.aborted)

class Bundle(unittest.TestCase):
    def test_cli_plan_and_approval_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in ('manifest','entry','recovery'):(root/name).write_text(name)
            args=[sys.executable,runner.__file__,'plan','--manifest',str(root/'manifest'),'--entry-manifest',str(root/'entry'),'--recovery-manifest',str(root/'recovery'),'--nonces','31','32','33']
            result=subprocess.run(args,capture_output=True,text=True,check=True)
            prepared=json.loads(result.stdout);approval=prepared['approval_template']
            approval['authorization']='APPROVED_FOR_PHYSICAL_EXECUTION'
            approval['operations']={key:True for key in approval['operations']}
            (root/'plan.json').write_text(result.stdout);(root/'approval.json').write_text(json.dumps(approval))
            result=subprocess.run([sys.executable,runner.__file__,'check-approval','--plan',str(root/'plan.json'),'--approval',str(root/'approval.json')],capture_output=True,text=True,check=True)
            self.assertIn('NO_OPERATIONS_EXECUTED',result.stdout)

    def test_full_raw_bundle_replay_and_tamper_rejection(self):
        from test_m5_series import complete,ABI,SOURCE
        data=complete()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);index=dict(schema=1,source=SOURCE,clock=data['clock'],boots=[])
            def write_jsonl(name,rows):
                (root/name).write_text(''.join(json.dumps(row)+'\n' for row in rows));return name
            for number,boot in enumerate(data['boots']):
                name='boot-'+str(number);folder=root/name;folder.mkdir();raw=b'';receive=[]
                for row in boot['uart']:
                    record=row['record']
                    text='M5_BOOT source='+record['source'] if record['event']=='BOOT' else 'M5 '+record['event']+' '+' '.join(str(k)+'='+str(v) for k,v in record.items() if k!='event')
                    chunk=(text+'\r\n').encode('ascii');raw+=chunk;receive.append(dict(monotonic=row['monotonic'],hex=chunk.hex()))
                (folder/'uart-rx.bin').write_bytes(raw)
                write_jsonl(name+'/receive.jsonl',receive)
                start=boot['uart'][0]['monotonic']-.01;end=boot['uart'][-1]['monotonic']+.1;polls=[]
                while start<end:polls.append(dict(monotonic=start,gap=.1));start+=.1
                polls.append(dict(monotonic=end,gap=.1));write_jsonl(name+'/poll.jsonl',polls)
                index['boots'].append(dict(nonce=boot['nonce'],reset=boot['reset'],uart_directory=name,writes_file=write_jsonl(name+'/writes.jsonl',boot['writes']),controls_file=write_jsonl(name+'/controls.jsonl',boot['controls']),peer_stops_file=write_jsonl(name+'/peer-stops.jsonl',boot['peer_stops']),peer_files={phase:write_jsonl(name+'/peer-'+phase+'.jsonl',rows) for phase,rows in boot['peers'].items()}))
            path=root/'index.json';path.write_text(json.dumps(index))
            self.assertEqual(runner.replay_bundle(path,SOURCE,ABI)['calls'],96)
            (root/'boot-1'/'uart-rx.bin').write_bytes(b'modified')
            with self.assertRaises(ValueError):runner.replay_bundle(path,SOURCE,ABI)

if __name__=='__main__':unittest.main()

class FullLoop(unittest.TestCase):
    def test_full_batched_boot_with_peer_reap_and_resetless_recovery(self):
        from test_m5_series import complete,ABI,SOURCE
        boot=complete()['boots'][0]
        class Clock:
            now=0.
            def __call__(self):return self.now
        class IO:
            def __init__(self,clock):
                self.clock=clock;self.phase=None;self.writes=[];self.stops=[];self.cues=[]
                self.events=[dict(kind='boot' if e['record']['event']=='BOOT' else 'target',**e) for e in boot['uart']]
                self.events += [dict(kind='peer_ready' if e['event']=='ready' else 'peer_request',monotonic=e['monotonic'],record=e) for rows in boot['peers'].values() for e in rows if e['event'] in ('ready','request')]
                self.events += [dict(kind='control',monotonic=e['monotonic'],record=e) for e in boot['controls'] if e['event']!='cable_disconnect_cue']
                self.events.sort(key=lambda e:e['monotonic'])
            def poll(self,timeout):
                self.assertion=timeout<=.25
                if not self.events:return []
                event=self.events.pop(0);self.clock.now=event['monotonic']+.0002
                return [event]
            def write_uart(self,line,*,deadline):self.writes.append(line);return self.clock() if callable(self.clock) else self.clock.now
            def prepare_peer(self,nonce,phase,count,mode):
                if self.phase is not None:raise AssertionError('Old peer still owns ports')
                self.phase=phase
            def stop_peer(self):
                if self.phase is None:return None
                phase=self.phase;self.phase=None;self.stops.append(phase)
                return dict(phase=phase,reaped=True)
            def notify_operator(self,row):self.cues.append(row)
        clock=Clock();io=IO(clock);s=runner.Supervisor(boot['nonce'],SOURCE,0,ABI)
        result=runner.run_boot(s,io,clock)
        self.assertEqual(result['status'],'BOOT_PROTOCOL_COMPLETE_PENDING_RAW_REPLAY')
        self.assertEqual(io.writes,[e['line'] for e in boot['writes']])
        self.assertEqual(io.stops,list(range(2,9)))
        self.assertEqual(len(io.cues),1)
        self.assertFalse(s.aborted)

class Bounds(unittest.TestCase):
    def test_missing_recovery_up_aborts_at_fifteen_seconds(self):
        s=runner.Supervisor(31,'123456789abc',0,{})
        s.state='prepared';s.sequence=8;s.ticket=9;s.peer_phase=7;s.stopped_phase=7
        s.controls=[dict(event='cable_disconnect_cue',monotonic=1.),dict(event='cable_disconnect_receipt',monotonic=1.1),dict(event='host_down',monotonic=1.2)]
        s.control(dict(event='cable_connect_receipt',monotonic=2.),2.1)
        with self.assertRaises(ValueError):s.tick(17.)
        self.assertTrue(s.aborted)
    def test_three_cli_boots_share_nonrenewable_series_deadline(self):
        prepared=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64)
        with tempfile.TemporaryDirectory() as directory:
            path,record=runner.series_session(directory,prepared,1,10.,"host-boot-A")
            record['completed']=[1];path.write_text(json.dumps(record))
            self.assertEqual(runner.series_session(directory,prepared,2,100.,"host-boot-A")[1]['started'],10.)
            with self.assertRaises(ValueError):runner.series_session(directory,prepared,2,1810.,"host-boot-A")
            with self.assertRaises(ValueError):runner.series_session(directory,prepared,3,100.,"host-boot-A")
            with self.assertRaises(ValueError):runner.series_session(directory,prepared,2,100.,"host-boot-B")

class CanonicalApproval(unittest.TestCase):
    def test_plan_and_approval_cannot_collude_to_remove_required_safety_operations(self):
        import copy
        original=runner.plan('a'*64,[31,32,33],'b'*64,'c'*64)
        for mutation in ['acquire_exclusive_probe_uart_and_nic','boot_1_cable_disconnect','qualification_reset_1','qualification_preflight_1','duplicate','extra','identity']:
            prepared=copy.deepcopy(original)
            if mutation=='duplicate':prepared['operations'].append(prepared['operations'][0])
            elif mutation=='extra':prepared['operations'].append('unapproved_extra')
            elif mutation=='identity':prepared['nic']['interface']='en0'
            else:prepared['operations'].remove(mutation)
            approval=runner.approval_template(prepared);approval['authorization']='APPROVED_FOR_PHYSICAL_EXECUTION';approval['operations']={key:True for key in approval['operations']}
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):runner.validate_approval(approval,prepared)
        self.assertEqual(runner.NIC['interface'],'en5')

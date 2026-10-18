import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
from capture_m3_gpio import validate_gpio, fresh_rows
import capture_m3_gpio as gpio
from unittest.mock import patch, MagicMock
import tempfile
import json
import subprocess


def fixture():
    rows=[(0,'*** Booting Zephyr OS build v4.4.0 ***'),
          (.01,'M3_BOOT source=0123456789ab profile=independent-health commands=0 fault_default=off'),
          (.02,'M3_RESET support_status=0 supported=0x1f cause_status=0 cause=0x1 clear_status=0 remaining_status=0 remaining=0x0')]
    presses={8,9,10,14,15,16,20,21,22,26,27,28,32,33,34}
    for n in range(1,52):
        rows.append((n+.001,f'M3_WORK sequence={n*10} monotonic_ms={n*1000} time=-4 pressed={int(n in presses)} device=0'))
        rows.append((n+.002,f'M3_OBSERVER_ALIVE monotonic_ms={n*1000} healthy=1 sequence={n*10} readiness=0'))
        rows.append((n+.003,'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=2048 main_unused=1500'))
    return rows


class GPIOControls(unittest.TestCase):
    def test_static_diagnostic_does_not_replace_qualification(self):
        released=[r for r in fixture() if 1<=r[0]<6]
        self.assertTrue(gpio.diagnostic_state(released,1,0)['matches'])
        self.assertFalse(gpio.diagnostic_state(released,1,1)['matches'])
        held=[(t,s.replace('pressed=0','pressed=1')) for t,s in released]
        self.assertTrue(gpio.diagnostic_state(held,1,1)['matches'])
        with self.assertRaises(ValueError):gpio.diagnostic_state(released[:3],1,0)
        with self.assertRaises(ValueError):gpio.diagnostic_state(released+[(5.5,'M3_WDT_ARMED')],1,0)
        with self.assertRaises(ValueError):
            gpio.diagnostic_state([r for r in released if not(r[1].startswith('M3_WORK ') and int(r[0])==3)],1,0)

    def test_static_diagnostic_rejects_stale_ack_without_target_action(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(gpio,'Receiver') as reader, \
             patch.object(gpio,'commander') as command:
            path=Path(directory)
            (path/'operator-held.json').write_text('{}')
            with self.assertRaises(ValueError):gpio.capture_diagnostic(None,path)
            reader.assert_not_called()
            command.assert_not_called()

    def test_static_diagnostic_missing_ack_stops_and_requests_release(self):
        receiver=MagicMock()
        receiver.rows=[r for r in fixture() if 1<=r[0]<6]
        receiver.failure=None
        receiver.text.return_value='retained'
        clock=[0]
        def now():
            clock[0]+=.1
            return clock[0]
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(gpio,'Receiver',return_value=receiver), \
             patch.object(gpio,'drain'), patch.object(gpio,'commander') as command, \
             patch.object(gpio.time,'monotonic',side_effect=now), \
             patch.object(gpio.time,'sleep'), patch('builtins.print') as message:
            path=Path(directory)
            with self.assertRaisesRegex(ValueError,'acknowledgement timeout'):
                gpio.capture_diagnostic(None,path)
            command.assert_not_called()
            receiver.finish.assert_called_once()
            self.assertTrue((path/'failure.txt').is_file())
            self.assertTrue((path/'lines.json').is_file())
            self.assertTrue(any('DIAGNOSTIC_STOP' in str(c) for c in message.call_args_list))
            self.assertGreaterEqual(clock[0],35)
            self.assertLess(clock[0],40)

    def test_static_diagnostic_checks_ack_gap_and_final_tail(self):
        for mode in ('ack-gap', 'final-tail'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                path=Path(directory)
                receiver=MagicMock(); receiver.rows=[]; receiver.failure=None
                receiver.text.return_value='raw'
                clock=[0]
                def now():
                    clock[0]+=.005
                    return clock[0]
                def message(value, **unused):
                    if value=='DIAGNOSTIC_HOLD':
                        if mode=='ack-gap':receiver.rows.append((clock[0],'M3_BOOT unexpected'))
                        else:(path/'operator-held.json').write_text('{"state":"held"}')
                    if value=='DIAGNOSTIC_RELEASE':
                        (path/'operator-released.json').write_text('{"state":"released"}')
                calls=[0]
                def state(rows, since, expected):
                    calls[0]+=1
                    if calls[0]==4:
                        receiver.finish.assert_called_once()
                        raise ValueError('final tail fault')
                    return {'matches':True}
                with patch.object(gpio,'Receiver',return_value=receiver), patch.object(gpio,'drain'), \
                     patch.object(gpio.time,'monotonic',side_effect=now), patch.object(gpio.time,'sleep'), \
                     patch.object(gpio,'diagnostic_state',side_effect=state), \
                     patch('builtins.print',side_effect=message):
                    with self.assertRaisesRegex(ValueError,'boot/fault' if mode=='ack-gap' else 'final tail'):
                        gpio.capture_diagnostic(None,path)
                    self.assertFalse((path/'diagnostic-result.json').exists())
                    receiver.finish.assert_called_once()

    def test_spoken_cue_schedule_and_failure_evidence(self):
        for index in range(10):
            with tempfile.TemporaryDirectory() as directory, \
                 patch.object(gpio.time,'monotonic',side_effect=[100+4*index+.02,100+4*index+.8]), \
                 patch.object(gpio,'speak') as speak:
                speak.return_value=None
                gpio.spoken_cue(index,100,Path(directory))
                self.assertEqual(speak.call_args.args[0][0], '누르세요' if index%2==0 else '놓으세요')
                event=json.loads((Path(directory)/'spoken-cues.jsonl').read_text())
                self.assertEqual(event['index'],index)
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(gpio.time,'monotonic',return_value=101), \
             patch.object(gpio,'speak') as speak:
            with self.assertRaises(ValueError):gpio.spoken_cue(0,100,Path(directory))
            speak.assert_not_called()
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(gpio.time,'monotonic',side_effect=[100.02,102.02]), \
             patch.object(gpio,'speak',side_effect=subprocess.TimeoutExpired('say',2)):
            with self.assertRaises(subprocess.TimeoutExpired):gpio.spoken_cue(0,100,Path(directory))
            event=json.loads((Path(directory)/'spoken-cues.jsonl').read_text())
            self.assertIn('TimeoutExpired',event['error'])

    def test_speech_unreaped_child_is_bounded_and_stops(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(gpio.subprocess,'Popen') as create, \
             patch.object(gpio.os,'killpg') as kill:
            child=create.return_value
            child.pid=12345
            child.wait.side_effect=subprocess.TimeoutExpired('say',2)
            with self.assertRaises(subprocess.TimeoutExpired):
                gpio.speak(['test'],2,Path(directory)/'speech.log')
            self.assertEqual(child.wait.call_count,2)
            self.assertEqual(child.wait.call_args.kwargs['timeout'],.5)
            kill.assert_called_once_with(12345,gpio.signal.SIGKILL)
            create.assert_called_once()

    def test_exact_five_groups_with_valid_normal_source(self):
        self.assertEqual(validate_gpio(fixture(),'0123456789ab',6,51)['presses'],5)

    def test_missing_extra_short_press_and_missing_final_release_fail(self):
        for change in ('missing','extra','short','final','baseline'):
            rows=fixture()
            for i,(t,line) in enumerate(rows):
                if not line.startswith('M3_WORK '):continue
                n=int(t)
                value=None
                if change=='missing' and n in (8,9,10):value=0
                if change=='extra' and n in (44,45,46):value=1
                if change=='short' and n in (9,10):value=0
                if change=='final' and n>=35:value=1
                if change=='baseline' and n==2:value=1
                if value is not None:
                    rows[i]=(t,line.replace('pressed=0',f'pressed={value}').replace('pressed=1',f'pressed={value}'))
            with self.subTest(change=change),self.assertRaises(ValueError):
                validate_gpio(rows,'0123456789ab',6,51)

    def test_missing_work_or_buffered_or_bad_identity_or_window_fail(self):
        cases=[]
        rows=fixture();rows=[r for r in rows if not(r[1].startswith('M3_WORK ') and int(r[0])==20)];cases.append((rows,'0123456789ab',6,51))
        rows=fixture();index=next(i for i,r in enumerate(rows) if r[1].startswith('M3_WORK ') and int(r[0])==20);rows[index]=(rows[index][0]+.4,rows[index][1]);rows.sort();cases.append((rows,'0123456789ab',6,51))
        cases.append((fixture(),'wrong',6,51));cases.append((fixture(),'0123456789ab',6,60))
        for data,source,begin,end in cases:
            with self.subTest(source=source,end=end),self.assertRaises(ValueError):
                validate_gpio(data,source,begin,end)

    def test_only_known_unarmed_prefix_can_precede_one_fresh_boot(self):
        rows=fixture()
        self.assertEqual(fresh_rows([(-1,'M3_WORK sequence=1 monotonic_ms=1000 time=-4 pressed=0 device=0')]+rows),rows)
        for prefix in ('M3_BOOT source=stale','M3_WDT_ARMED','M3_FAULT_STALL'):
            with self.assertRaises(ValueError):fresh_rows([(-1,prefix)]+rows)
        with self.assertRaises(ValueError):fresh_rows(rows+[(60,'*** Booting Zephyr')])

    def test_no_cue_or_extra_hardware_after_invalid_start(self):
        for condition in ('held', 'wrong-source', 'reader-failed'):
            rows=[r for r in fixture() if r[0]<6]
            if condition=='held':
                rows=[(t,line.replace('pressed=0','pressed=1')) for t,line in rows]
            if condition=='wrong-source':
                rows=[(t,line.replace('source=0123456789ab','source=wrong')) for t,line in rows]
            receiver=MagicMock()
            receiver.rows=rows
            receiver.failure=ValueError('reader failed') if condition=='reader-failed' else None
            receiver.text.return_value='\n'.join(line for _,line in rows)
            clock=[0]
            def now():
                clock[0]+=.1
                return clock[0]
            with self.subTest(condition=condition), tempfile.TemporaryDirectory() as directory, \
                 patch.object(gpio,'Receiver',return_value=receiver), \
                 patch.object(gpio,'drain'), patch.object(gpio,'commander') as command, \
                 patch.object(gpio.time,'monotonic',side_effect=now), \
                 patch.object(gpio.time,'sleep'), patch('builtins.print') as cue:
                with self.assertRaises(ValueError):
                    gpio.capture_gpio(None,Path(directory),'0123456789ab')
                cue.assert_not_called()
                self.assertEqual(command.call_count,0 if condition=='reader-failed' else 1)
                if command.call_count:
                    self.assertEqual(command.call_args.args[1],'r\ng\nq\n')
                self.assertTrue((Path(directory)/'failure.txt').is_file())
                self.assertTrue((Path(directory)/'lines.json').is_file())
                receiver.finish.assert_called_once()

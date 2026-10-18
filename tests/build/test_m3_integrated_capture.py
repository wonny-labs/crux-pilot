import sys,tempfile,unittest
from pathlib import Path
from unittest.mock import Mock,patch,MagicMock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import capture_m3_integrated as run
import types

class IntegratedCaptureControls(unittest.TestCase):
    def series(self,d):
        s=object.__new__(run.Series)
        s.directory=Path(d)/'series';s.directory.mkdir()
        s.end=run.time.monotonic()+1800;s.case_end=s.end
        s.guard={};s.images={'original':{'binary':'/frozen/normal.bin'}}
        s.args=Mock(uart='/unused');s.lease={'process':None,'log':None}
        s.target=Mock(return_value='Verify successful')
        s.healthy_body=Mock(return_value='healthy');s.watchdog_body=Mock(return_value='watchdog')
        s.loss_body=Mock();s.qualify=Mock();s.operator=Mock();s.wait=Mock()
        return s
    def execute(self,s):
        serial=types.ModuleType('serial')
        serial.Serial=MagicMock()
        serial.tools=types.ModuleType('serial.tools')
        serial.tools.list_ports=types.ModuleType('serial.tools.list_ports')
        serial.tools.list_ports.comports=Mock(return_value=[])
        modules={'serial':serial,'serial.tools':serial.tools,'serial.tools.list_ports':serial.tools.list_ports}
        with patch.dict(sys.modules,modules),patch.object(run,'speak'),patch.object(run.net,'guard_check'),patch.object(run.subprocess,'check_output',return_value=''),patch.object(run.fcntl,'flock'),patch.object(run.fcntl,'ioctl'),patch.object(run,'check_owners'),patch.object(run,'select_uart'),patch.object(serial.tools.list_ports,'comports',return_value=[]),patch.object(serial,'Serial'),patch.object(run.net,'check_route',return_value=True),patch.object(run.time,'sleep'),patch.object(run.net,'release_peer'):
            s.execute()
    def test_only_functional_failure_restores_once(self):
        for error,restores in ((run.FunctionalFailure('oracle'),1),(run.HardwareStop('UART/probe/guard/log'),0),(ValueError('unknown'),0)):
            with self.subTest(error=error),tempfile.TemporaryDirectory() as d:
                s=self.series(d)
                s.capture=Mock(side_effect=[error,(Path(d),Mock(),{})])
                with self.assertRaises((run.FunctionalFailure,run.HardwareStop)):self.execute(s)
                self.assertEqual(s.capture.call_count,1+restores)
                if restores:self.assertEqual(s.capture.call_args.args,('failed-qualification-normal-restoration','program',None,'normal'))
                self.assertEqual([c.args for c in s.target.call_args_list],[('q\n','entry-identity'),('verifybin "/frozen/normal.bin", 0x60000000\nq\n','entry-known-normal')])
    def test_guard_delay_stops_before_commander(self):
        with tempfile.TemporaryDirectory() as d:
            s=self.series(d);del s.target
            s.end=1000;s.case_end=100
            with patch.object(run.time,'monotonic',side_effect=[0,0,70,70]),patch.object(run.net,'guard_check'),patch.object(run,'commander') as commander:
                with self.assertRaises(run.HardwareStop):s.target('r\ng\nq\n','x')
                commander.assert_not_called()
    def test_exact_case_order_without_retry(self):
        with tempfile.TemporaryDirectory() as d:
            s=self.series(d)
            def captured(name,*args):
                case=Path(d)/name;case.mkdir();return case,Mock(),{'status':'PASS'}
            s.capture=Mock(side_effect=captured)
            self.execute(s)
            self.assertEqual([c.args[0] for c in s.capture.call_args_list],['F1-gpio-window','F2-physical-link-loss','F3-explicit-link-recovery','watchdog-time-1','watchdog-time-2','watchdog-time-3','watchdog-network-1','watchdog-network-2','watchdog-network-3','final-integrated-normal'])
            self.assertEqual(s.qualify.call_count,9)



    def test_final_normal_checks_tail_after_receiver_finish(self):
        from test_m3_integrated_oracle import functional_fixture,SOURCE,BASE
        rows,_=functional_fixture()
        baseline=[(t,line.replace('commands=1','commands=0').replace('network_active=1','network_active=0')) for t,line in rows if t<BASE+5 and not any(name in line for name in ('M3_NET_WINDOW ','M3_NET_EXCHANGE ','M3_NET_CLEANUP '))]
        for tail in ('M3_NET_WORK sequence=1 completed_ms=4000 result=0','FATAL ERROR after baseline'):
            with self.subTest(tail=tail),tempfile.TemporaryDirectory() as d:
                s=self.series(d);s.source=SOURCE;s.images['normal']={'binary':'/unused'}
                s.port=Mock()
                s.wait=Mock(side_effect=lambda predicate,*args:self.assertTrue(predicate()))
                rx=Mock(rows=list(baseline));rx.text.return_value='\n'.join(line for _,line in baseline)
                def finish(receiver):receiver.rows.append((BASE+4,tail))
                with patch.object(run,'drain'),patch.object(run,'Receiver',return_value=rx),patch.object(run.net,'finish_receiver',side_effect=finish),patch.object(run.net,'release_peer'):
                    with self.assertRaises(ValueError):s.capture('final-normal','reset',None,'normal')
                self.assertTrue((s.directory/'final-normal/lines.json').exists())
                self.assertFalse((s.directory/'final-normal/result.json').exists())



    def test_initial_observations_exclude_old_queued_rows(self):
        prefix=[(float(n),'M3_OBSERVER_ALIVE monotonic_ms=90000') for n in range(5)]
        fresh=[(10.,'*** Booting Zephyr fixture ***'),(10.1,'M3_BOOT source=expected profile=integrated commands=1 fault_default=off')]
        fresh += [(11.+n,f'M3_OBSERVER_ALIVE monotonic_ms={1000*n}') for n in range(2)]
        self.assertEqual(run.initial_rows(prefix,'expected',1),[])
        self.assertEqual(len(run.records(run.initial_rows(prefix+fresh,'expected',1),'M3_OBSERVER_ALIVE')),2)
        with self.assertRaises(ValueError):run.initial_rows(prefix+fresh,'wrong',1)
        with self.assertRaises(ValueError):run.initial_rows(fresh+fresh,'expected',1)
        late=list(fresh);late[1]=(10.6,late[1][1])
        with self.assertRaises(ValueError):run.initial_rows(late,'expected',1)



    def test_initial_fifth_stack_must_complete_once_in_order(self):
        from test_m3_integrated_oracle import functional_fixture,SOURCE,BASE
        rows,_=functional_fixture()
        fresh=[(t,line) for t,line in rows if t<BASE+5]
        index=[i for i,(_,line) in enumerate(fresh) if 'M3_STACK ' in line][4]
        pending=fresh[:index]+fresh[index+1:]
        self.assertEqual(run.initial_checkpoint_rows(pending,SOURCE,1),[])
        accepted=run.initial_checkpoint_rows(fresh,SOURCE,1)
        self.assertEqual(len(run.records(accepted,'M3_OBSERVER_ALIVE')),5)
        for altered in (fresh[:index]+[fresh[index]]+fresh[index:],
                        fresh[:index-1]+[fresh[index],fresh[index-1]]+fresh[index+1:],
                        fresh[:index]+[(fresh[index][0]+.3,fresh[index][1])]+fresh[index+1:]):
            with self.assertRaises(ValueError):run.initial_checkpoint_rows(altered,SOURCE,1)

if __name__=='__main__':unittest.main()

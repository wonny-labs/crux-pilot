import sys,tempfile,unittest
from pathlib import Path
from unittest.mock import Mock,patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import capture_m3_integrated as run
import serial
import serial.tools.list_ports

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
        with patch.object(run,'speak'),patch.object(run.net,'guard_check'),patch.object(run.subprocess,'check_output',return_value=''),patch.object(run.fcntl,'flock'),patch.object(run.fcntl,'ioctl'),patch.object(run,'check_owners'),patch.object(run,'select_uart'),patch.object(serial.tools.list_ports,'comports',return_value=[]),patch.object(serial,'Serial'),patch.object(run.net,'check_route',return_value=True),patch.object(run.time,'sleep'),patch.object(run.net,'release_peer'):
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
        baseline=[(t,line.replace('commands=1','commands=0')) for t,line in rows if t<BASE+4]
        for tail in ('M3_NET_WORK sequence=1 completed_ms=4000 result=0','FATAL ERROR after baseline'):
            with self.subTest(tail=tail),tempfile.TemporaryDirectory() as d:
                s=self.series(d);s.source=SOURCE;s.images['normal']={'binary':'/unused'}
                s.port=Mock()
                rx=Mock(rows=list(baseline));rx.text.return_value='\n'.join(line for _,line in baseline)
                def finish(receiver):receiver.rows.append((BASE+4,tail))
                with patch.object(run,'drain'),patch.object(run,'Receiver',return_value=rx),patch.object(run.net,'finish_receiver',side_effect=finish),patch.object(run.net,'release_peer'):
                    with self.assertRaises(ValueError):s.capture('final-normal','reset',None,'normal')
                self.assertTrue((s.directory/'final-normal/lines.json').exists())
                self.assertFalse((s.directory/'final-normal/result.json').exists())

if __name__=='__main__':unittest.main()

import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3_network as runner


class NetworkCaptureControls(unittest.TestCase):
    def test_guard_uncertainty_is_hardware_stop(self):
        with patch.object(runner, '_guard_check', side_effect=ValueError('changed')):
            with self.assertRaises(runner.HardwareStop): runner.guard_check({}, None, 'x')

    def test_route_uncertainty_is_hardware_stop(self):
        with patch.object(runner, '_check_route', side_effect=ValueError('wrong interface')):
            with self.assertRaises(runner.HardwareStop): runner.check_route(None, 'x')

    def test_reader_uncertainty_is_hardware_stop(self):
        receiver = Mock()
        receiver.finish.side_effect = ValueError('partial/corrupted')
        with self.assertRaises(runner.HardwareStop): runner.finish_receiver(receiver)

    def test_expired_peer_cannot_supply_link_failure(self):
        peer = Mock()
        peer.poll.return_value = 0
        with self.assertRaises(runner.HardwareStop): runner.stop_peer(peer)
        peer.send_signal.assert_not_called()

    def test_peer_ignoring_term_is_killed_bounded_and_stops(self):
        peer = Mock(pid=123)
        peer.poll.return_value = None
        peer.wait.side_effect = [runner.subprocess.TimeoutExpired('peer',3), 0]
        with patch.object(runner.os, 'killpg') as kill:
            with self.assertRaises(runner.HardwareStop): runner.stop_peer(peer)
            kill.assert_called_once_with(123, runner.signal.SIGKILL)
        self.assertEqual([c.kwargs for c in peer.wait.call_args_list], [{'timeout':3},{'timeout':1}])

    def test_link_down_allows_absent_ip_but_checks_adapter_identity(self):
        with patch.object(runner, 'run_checked', return_value='ether 00:0a:cd:41:89:cc\nstatus: inactive'):
            self.assertFalse(runner.check_link(Path('/unused'), 'down'))
        with patch.object(runner, 'run_checked', return_value='ether 00:0a:cd:41:89:cb\nstatus: inactive'):
            with self.assertRaises(runner.HardwareStop): runner.check_link(Path('/unused'), 'down')

    def test_current_guard_usb_partition_and_deadline_controls(self):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp); executable=d/'guard'; executable.write_bytes(b'guard')
            log=d/'guard.log'; log.write_text('READY\nDENIED exact_DAPLink_USB_serial='+runner.USB_SERIAL+' disk=disk4\n')
            session=dict(clock=runner.time.CLOCK_NAME,serial=runner.USB_SERIAL,disk='/dev/disk4', executable=str(executable),
                         sha256=runner.sha(executable), deadline=runner.time.monotonic()+5000,
                         pid=123,process_identity='123 guard start',log=str(log))
            tree=[{'USB Serial Number':runner.USB_SERIAL,'idVendor':0x0d28,'idProduct':0x0204,
                   'children':[{'BSD Name':'disk4'},{'IOCalloutDevice':'/dev/cu.usbmodem11302'}]}]
            query=runner.subprocess.CompletedProcess([],0,runner.plistlib.dumps(tree).decode(),'')
            with patch.object(runner,'inventory',return_value=query), patch.object(runner,'run_checked',side_effect=['123 guard start','/dev/disk5 on /Volumes/OTHER (x)']):
                runner.guard_check(session,d,'ok')
            for mounted in ('/dev/disk4 on /Volumes/RENAMED (x)', '/dev/disk4s1 on /Volumes/RENAMED (x)', '/dev/disk4s1s2 on /Volumes/RENAMED (x)'):
                with patch.object(runner,'inventory',return_value=query), patch.object(runner,'run_checked',side_effect=['123 guard start',mounted]):
                    with self.assertRaises(runner.HardwareStop): runner.guard_check(session,d,'mounted')
            tree[0]['USB Serial Number']='other'
            query.stdout=runner.plistlib.dumps(tree).decode()
            with patch.object(runner,'inventory',return_value=query), patch.object(runner,'run_checked',return_value='123 guard start'):
                with self.assertRaises(runner.HardwareStop): runner.guard_check(session,d,'wrong-usb')
            session['deadline']=runner.time.monotonic()+10
            with self.assertRaises(runner.HardwareStop): runner.guard_check(session,d,'expiry',90)

    def test_guard_rejects_changed_boot_before_clock_pid_or_usb_queries(self):
        with tempfile.TemporaryDirectory() as temporary:
            session={'clock':runner.time.CLOCK_NAME,'serial':runner.USB_SERIAL,
                     'disk':'/dev/disk4','boot_identity':'original boot'}
            with patch.object(runner,'run_checked',return_value='new boot') as query, \
                 patch.object(runner.time,'monotonic') as clock:
                with self.assertRaises(runner.HardwareStop):runner.guard_check(session,Path(temporary),'reboot')
                query.assert_called_once()
                self.assertEqual(query.call_args.args[0],['/usr/sbin/sysctl','-n','kern.boottime'])
                clock.assert_not_called()

    def test_wait_is_clamped_by_global_series_deadline(self):
        with patch.object(runner,'SERIES_DEADLINE',1.), patch.object(runner.time,'monotonic',return_value=2.):
            predicate=Mock(return_value=True)
            with self.assertRaises(ValueError): runner.wait_until(predicate,90)
            predicate.assert_not_called()

    def test_readiness_and_teardown_failures_close_once_without_retry(self):
        for failure in (None, runner.HardwareStop('timeout'), ValueError('nonzero')):
            lease=dict(process=Mock(),log=Mock())
            log=lease['log']
            with patch.object(runner,'stop_peer',side_effect=failure) as stop:
                try: runner.release_peer(lease)
                except (runner.HardwareStop,ValueError): pass
                runner.release_peer(lease)
                runner.release_peer(lease)
                stop.assert_called_once()
            log.close.assert_called_once()
            self.assertEqual(lease,dict(process=None,log=None))

    def test_boot_checkpoint_excludes_duplicate_initial_reports(self):
        rows=[(t,f'M3_NET_STATE monotonic_ms={ms} link={link} address=1 service_ready=0')
              for t,ms,link in ((10.,0,0),(10.,0,0),(11.,1000,0),(12.,2000,1),(13.,3000,1))]
        self.assertEqual(runner.initial_checkpoint_boundary(rows),12.25)
        with self.assertRaises(ValueError): runner.initial_checkpoint_boundary(rows[:3])

    def test_fresh_boot_excludes_queued_pre_reset_state(self):
        source='known'
        old=[(10.,'M3_NET_STATE monotonic_ms=695696 link=1 address=1 service_ready=0')]
        fresh=[(11.,'*** Booting Zephyr OS build v4.4.0 ***'),
               (11.1,'M3_NET_BOOT source=known profile=network-probe'),
               (12.,'M3_NET_STATE monotonic_ms=1561 link=0 address=1 service_ready=0'),
               (13.,'M3_NET_STATE monotonic_ms=2582 link=0 address=1 service_ready=0'),
               (14.,'M3_NET_STATE monotonic_ms=3602 link=1 address=1 service_ready=0')]
        self.assertEqual(runner.fresh_network_rows(old,source),[])
        self.assertEqual(runner.initial_checkpoint_boundary(runner.fresh_network_rows(old+fresh,source)),14.25)
        with self.assertRaises(ValueError):runner.initial_checkpoint_boundary(runner.fresh_network_rows(old+fresh[:-1],source))
        with self.assertRaises(ValueError):runner.fresh_network_rows(old+fresh,'wrong')
        with self.assertRaises(ValueError):runner.fresh_network_rows(fresh+fresh,source)
        with self.assertRaises(ValueError):runner.fresh_network_rows([(10.,'M3_NET_EXCHANGE result=0')]+fresh,source)
        with self.assertRaises(ValueError):runner.fresh_network_rows([(10.,'M3_NET_STATE service_ready=1')]+fresh,source)
        late=list(fresh);late[1]=(11.6,late[1][1])
        with self.assertRaises(ValueError):runner.fresh_network_rows(late,source)

    def test_explicit_previous_image_is_verified_before_new_programming(self):
        new={'profile':'network','binary':'/new.bin'}
        old={'profile':'network','binary':'/old.bin'}
        normal={'profile':'normal','binary':'/normal.bin'}
        self.assertEqual(runner.select_entry_image('previous-network',new,normal,old),(old,True))
        self.assertEqual(runner.select_entry_image('existing-network',new,normal),(new,False))
        self.assertEqual(runner.select_entry_image('original-normal',new,normal),(normal,True))
        for mode,prior in (('previous-network',None),('previous-network',normal),('existing-network',old),('original-normal',old),('unknown',None)):
            with self.assertRaises(ValueError):runner.select_entry_image(mode,new,normal,prior)

    def test_existing_image_resumption_never_programs_or_erases(self):
        self.assertEqual(runner.image_commands('/known/image.bin',False),'r\ng\nq\n')
        commands=runner.image_commands('/known/image.bin',True)
        self.assertEqual(commands.count('loadfile '),1)
        self.assertEqual(commands.count('verifybin '),1)
        self.assertNotIn('erase',commands.lower())
        for path in ('/bad/"image.bin','/bad/\nimage.bin'):
            with self.assertRaises(ValueError):runner.image_commands(path,True)


if __name__ == '__main__': unittest.main()

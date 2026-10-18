import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3_network as runner


class NetworkCaptureControls(unittest.TestCase):
    def test_exact_normal_analyzer_prefix_without_global_whitespace_repair(self):
        prefix=[(0.,''),(0.,'Thread analyze:'),
            (0.,' m3-critical         : STACK: unused 3744 usage 352 / 4096 (8 %); CPU: 0 %'),
            (0.,'                      : Total CPU cycles used: 0')]
        fresh=[(1.,'*** Booting Zephyr'),(1.01,'M3_NET_BOOT source=abc profile=network-probe exposure=isolated-only')]
        self.assertEqual(runner.fresh_network_rows(prefix+fresh,'abc','normal',native_output=True),fresh)
        for extra in (' M3_WORK sequence=1 monotonic_ms=1 time=0 pressed=0 device=0',
                      '\x1b[0mThread analyze:','bad : Total CPU cycles used: 0',
                      'M3_NET_REQUESTED','FATAL ERROR'):
            with self.assertRaises(ValueError):runner.fresh_network_rows([(0.,extra)]+fresh,'abc','normal',native_output=True)
        with self.assertRaises(ValueError):runner.fresh_network_rows(prefix+fresh,'wrong','normal',native_output=True)
    def test_reset_censored_analyzer_name_is_last_normal_row_only(self):
        fragment=(0.,' idle      ')
        fresh=[(1.,'*** Booting Zephyr'),(1.01,'M3_NET_BOOT source=abc profile=network-probe exposure=isolated-only')]
        self.assertEqual(runner.fresh_network_rows([fragment]+fresh,'abc','normal',native_output=True),fresh)
        self.assertEqual(runner.normal_reset_fragment([fragment]),fragment[1])
        for prefix in ([fragment,(0.,'Thread analyze:')],[(0.,' unknown      ')],[(0.,' idle x')],
                       [(0.,'\x1b[0midle')],[(0.,' M3_NET_REQUESTED')]):
            with self.assertRaises(ValueError):runner.fresh_network_rows(prefix+fresh,'abc','normal',native_output=True)
        with self.assertRaises(ValueError):runner.fresh_network_rows([fragment]+fresh,'abc',native_output=True)

    def test_typed_native_logger_idle_prefix_is_profile_gated(self):
        prefix=[(0.,'M3_NET_RESOURCE monotonic_ms=1 owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32'),
            (0.,'M3_NET_STATE monotonic_ms=1 link=0 address=1 service_ready=0'),
            (0.,'M3_NET_STACK status=0 usable=4096 unused=2000'),
            (0.,'M3_NET_LOGGER monotonic_ms=1 queue_snapshot_ms=1 found=1 status=0 configured=2048 usable=2048 unused=1560 priority=14 queued=0')]
        fresh=[(1.,'*** Booting Zephyr'),(1.01,'M3_NET_BOOT source=abc profile=network-probe exposure=isolated-only')]
        self.assertEqual(runner.fresh_network_rows(prefix+fresh,'abc',native_output=True),fresh)
        with self.assertRaises(ValueError):runner.fresh_network_rows(prefix+fresh,'abc')
        for extra in ('M3_NET_REQUESTED','M3_MDIO_FIRST_FAILURE valid=1','M3_OUTPUT_START window_ms=15000',
                      prefix[-1][1]+' ', ' '+prefix[-1][1]):
            with self.assertRaises(ValueError):runner.fresh_network_rows(prefix+[(0.,extra)]+fresh,'abc',native_output=True)

    def test_phy_image_cannot_be_used_for_full_qualification(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / '.config'
            config.write_text('CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y\n')
            with self.assertRaises(ValueError): runner.phy_diagnostic_mode(config, False)
            self.assertTrue(runner.phy_diagnostic_mode(config, True))
            config.write_text('# CONFIG_CRUX_M3_PHY_DIAGNOSTIC is not set\n')
            self.assertFalse(runner.phy_diagnostic_mode(config, False))

    def test_phy_observation_requires_complete_plausible_ordered_reads(self):
        fields = ' eir=0 eimr=0 ecr=0 rcr=0 tcr=0 rdar=0 tdar=0 mscr=0 mmfr=0'
        rows = [(1., 'M3_NET_MAC phase=1 monotonic_ms=1' + fields),
                (2., 'M3_NET_MAC phase=2 monotonic_ms=2' + fields),
                (3., 'M3_NET_PHY_CACHED result=0 monotonic_ms=3 link=1 speed=1 admin=1 running=1 carrier=1 oper=1')]
        for i, reg in enumerate((0, 1, 1, 2, 3, 4, 5, 31)):
            value = 0x22 if i == 3 else 0x1560
            rows.append((4. + i, f'M3_NET_PHY_READ index={i} reg={reg} result=0 value={value} monotonic_ms={4+i}'))
        rows.append((12., 'M3_NET_PHY_DONE reads=8 status=0 plausible=1 monotonic_ms=12'))
        self.assertEqual(runner.phy_diagnostic_result(rows)['status'],
                         'OBSERVED_PLAUSIBLE_NOT_TRANSACTION_PROOF')
        bad = [rows[:-1], rows[:3] + rows[4:], rows + rows[-1:]]
        for before, after in (('index=0', 'index=7'), ('reg=31', 'reg=30'),
                              ('value=34', 'value=0'), ('value=5472', 'value=65535'),
                              ('status=0', 'status=-5'), ('plausible=1', 'plausible=0'),
                              ('eir=0', 'eir=4294967296'), (' rcr=0', ''),
                              ('result=0', 'result=-5'), ('monotonic_ms=11', 'monotonic_ms=0')):
            bad.append([(t, line.replace(before, after)) for t, line in rows])
        for invalid in bad:
            with self.assertRaises(ValueError): runner.phy_diagnostic_result(invalid)

    def test_diagnostic_cannot_publish_seven_stage_qualification(self):
        self.assertEqual(runner.stage_modes(True), ('echo',))
        result = runner.completed_series(True, 1)
        self.assertEqual(result['status'], 'DIAGNOSTIC_PASS')
        self.assertFalse(result['qualification'])
        for diagnostic, count in ((True, 0), (True, 7), (False, 1), (False, 6)):
            with self.assertRaises(ValueError): runner.completed_series(diagnostic, count)
        self.assertEqual(runner.stage_modes(False), ('echo','silence','echo','close','echo','echo','echo'))
        self.assertEqual(runner.completed_series(False, 7)['status'], 'PASS')

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
            boot_uuid = 'D83ABC6F-EEB9-48AF-9A24-E503879FAE62\n'
            session.update(boot_session_uuid=boot_uuid, boot_identity='wall-clock-adjusted legacy value')
            with patch.object(runner,'inventory',return_value=query), patch.object(runner,'run_checked',
                    side_effect=[boot_uuid,'123 guard start','/dev/disk5 on /Volumes/OTHER (x)']):
                runner.guard_check(session,d,'uuid-stable-despite-clock-adjustment')
            session.pop('boot_session_uuid');session.pop('boot_identity')
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

    def test_guard_rejects_changed_uuid_before_clock_pid_or_usb_queries(self):
        with tempfile.TemporaryDirectory() as temporary:
            session={'clock':runner.time.CLOCK_NAME,'serial':runner.USB_SERIAL,
                     'disk':'/dev/disk4','boot_session_uuid':'D83ABC6F-EEB9-48AF-9A24-E503879FAE62\n'}
            with patch.object(runner,'run_checked',return_value='00000000-0000-0000-0000-000000000001\n') as query, \
                 patch.object(runner.time,'monotonic') as clock:
                with self.assertRaises(runner.HardwareStop):runner.guard_check(session,Path(temporary),'new-boot')
                query.assert_called_once()
                self.assertEqual(query.call_args.args[0],['/usr/sbin/sysctl','-n','kern.bootsessionuuid'])
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

    def test_verified_normal_prefix_is_preserved_but_never_counts_as_new_boot(self):
        old = [(1., line) for line in (
            'M3_WORK sequence=8941 monotonic_ms=894050 time=-4 pressed=0 device=0',
            'M3_OBSERVER_ALIVE monotonic_ms=894489 healthy=1 sequence=8945 readiness=0',
            'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3744 main_status=0 main_usable=2048 main_unused=1352',
            'Thread analyze:',
            'm3-critical         : STACK: unused 3744 usage 352 / 4096 (8 %); CPU: 0 %',
            ': Total CPU cycles used: 3205900825',
            'ISR0                : STACK: unused 1808 usage 240 / 2048 (11 %); CPU: 0 %')]
        fresh = [(2., '*** Booting Zephyr OS build v4.4.0 ***'),
                 (2.1, 'M3_NET_BOOT source=known profile=network-probe')]
        self.assertEqual(runner.fresh_network_rows(old, 'known', 'normal'), [])
        self.assertEqual(runner.fresh_network_rows(old + fresh, 'known', 'normal'), fresh)
        with self.assertRaises(ValueError): runner.fresh_network_rows(old + fresh, 'known')
        for bad in ('M3_FAULT kind=time', 'M3_WORK broken', 'unexpected text',
                    'M3_OBSERVER_ALIVE monotonic_ms=1 healthy=0 sequence=1 readiness=0'):
            with self.assertRaises(ValueError):
                runner.fresh_network_rows(old + [(1.5, bad)] + fresh, 'known', 'normal')
        with self.assertRaises(ValueError): runner.fresh_network_rows(old + fresh, 'wrong', 'normal')
        with self.assertRaises(ValueError): runner.fresh_network_rows(old + fresh + fresh, 'known', 'normal')

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
        # Let loadfile initialise the programming context once. Execution must
        # remain after full verification, with no early reset/run or retry.
        lines=commands.splitlines()
        self.assertEqual([line.split()[0] for line in lines],
                         ['loadfile','verifybin','r','g','q'])
        self.assertEqual(lines[0].split(', ')[1],'0x60000000')
        self.assertEqual(lines[1].split(', ')[1],'0x60000000')
        for path in ('/bad/"image.bin','/bad/\nimage.bin'):
            with self.assertRaises(ValueError):runner.image_commands(path,True)


if __name__ == '__main__': unittest.main()

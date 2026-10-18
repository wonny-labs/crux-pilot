import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
from m3_integrated_output import logger_groups, idle_logger_checkpoint, retained_prefix, planned_reset_rows
from m3_image import check_record_output
import capture_m3_integrated as capture


def fixture():
    rows=[]
    for n in range(5):
        stamp=n*1000
        t=10.+n
        rows += [(t,f'M3_OBSERVER_ALIVE monotonic_ms={stamp} healthy=0 sequence={n} readiness=0 network_sequence=0 network_completed_ms=0 network_active=0 network_status=0 window_end_ms=0'),
                 (t+.01,f'M3_WORK sequence={n} monotonic_ms={stamp} time=0 pressed=0 device=0'),
                 (t+.02,'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=2048 main_status=0 main_usable=4096 main_unused=2048 network_status=0 network_object=4160 network_usable=4096 network_unused=2048'),
                 (t+.03,f'M3_LOGGER monotonic_ms={stamp+1} queue_snapshot_ms={stamp} found=1 status=0 configured=2048 usable=2048 unused=1536 priority=14 queued=0')]
    return rows


class IntegratedNativeOutputControls(unittest.TestCase):
    def test_loss_capture_waits_for_post_ack_observation(self):
        # Loss and idle settling finished before a delayed human receipt.
        receiver=Mock(rows=[])
        state={'M3_OBSERVER_ALIVE':[
            dict(monotonic_ms=12000,received=99.,readiness=0,healthy=0)],
            'M3_NET_STATE':[dict(link=0,service_ready=0,received=95.)],
            'M3_NET_WINDOW_STOP':[dict(received=95.)]}
        series=Mock()
        series.start_window.return_value=dict(end_ms=60000)
        series.operator.return_value=dict(acknowledged=100.,requested=90.)
        seen=[]
        def wait(predicate,duration,rx):
            if not predicate():
                seen.append(duration)
                state['M3_OBSERVER_ALIVE'].append(dict(monotonic_ms=23000,
                    received=110.,readiness=0,healthy=0))
                self.assertTrue(predicate())
        series.wait.side_effect=wait
        with patch.object(capture,'records',side_effect=lambda rows,name:state[name]),\
             patch.object(capture.time,'monotonic',return_value=110.),\
             patch.object(capture.net,'check_link',return_value=False):
            capture.Series.loss_body(series,Path('/unused'),receiver)
        self.assertEqual(seen,[5.])

    def test_planned_reset_preserves_exact_new_epoch_and_old_fragment(self):
        banner='*** Booting Zephyr OS build v4.4.0 ***'
        rows=[(1.,'Thread analyze:'),(2.,' shell_uart          : STACK: '+banner),
              (2.1,'M3_BOOT source=ec94fbec22cd profile=integrated commands=0 fault_default=off')]
        result,excluded=planned_reset_rows(rows)
        self.assertEqual(result[1:],[(2.,banner),rows[2]])
        self.assertEqual(excluded,[dict(received=2.,fragment=' shell_uart          : STACK: ')])
        self.assertTrue(rows[1][1].startswith(' shell_uart'))

    def test_planned_reset_rejects_unrecognised_or_later_fragments(self):
        banner='*** Booting Zephyr OS build v4.4.0 ***'
        for prefix in ('garbage','M3_WORK ',' shell_uart          : STACK: unused 1 '):
            with self.subTest(prefix=prefix),self.assertRaises(ValueError):
                planned_reset_rows([(1.,'Thread analyze:'),(2.,prefix+banner)])
        for rows in ([(1.,' shell_uart          : STACK: '+banner)],
                     [(1.,banner),(2.,' shell_uart          : STACK: '+banner)],
                     [(1.,'Thread analyze:'),(2.,' shell_uart          : STACK: '+banner+'bad')]):
            with self.assertRaises(ValueError):planned_reset_rows(rows)

    def test_full_groups_and_first_three_idle_samples(self):
        rows=fixture()
        self.assertEqual(len(logger_groups(rows)),5)
        self.assertEqual(idle_logger_checkpoint(rows),dict(samples=3,min_unused=1536,queue=0))
        # A real watchdog epoch may reset target uptime, never the receive clock.
        second=[(t+10,s) for t,s in rows]
        self.assertEqual(len(logger_groups(rows+[(19.,'*** Booting Zephyr fixture ***')]+second)),10)

    def test_no_favourable_idle_resampling(self):
        rows=fixture();rows[7]=(rows[7][0],rows[7][1].replace('queued=0','queued=1'))
        with self.assertRaises(ValueError):idle_logger_checkpoint(rows)

    def test_missing_duplicate_reordered_or_late_group_rejected(self):
        rows=fixture()
        late=list(rows);late[3]=(rows[3][0]+.3,rows[3][1])
        swapped=list(rows);swapped[2],swapped[3]=swapped[3],swapped[2]
        for changed in (rows[:3]+rows[4:],rows[:3]+[rows[3]]+rows[3:],swapped,late,
                        rows[:3]+[(10.025,'*** Booting Zephyr cut ***')]+rows[3:]):
            with self.assertRaises(ValueError):logger_groups(changed)

    def test_full_capture_tail_loss_and_resources_are_checked(self):
        rows=fixture()
        for old,new in [('found=1','found=0'),('status=0','status=-1'),('usable=2048','usable=2112'),
                        ('unused=1536','unused=1023'),('priority=14','priority=15'),
                        ('queued=0','queued=16385'),('queue_snapshot_ms=4000','queue_snapshot_ms=3999')]:
            changed=list(rows);changed[-1]=(changed[-1][0],changed[-1][1].replace(old,new))
            with self.subTest(new=new),self.assertRaises(ValueError):logger_groups(changed)
        for tail in ('--- 1 messages dropped ---','M3_MDIO_FIRST_FAILURE valid=1','M3_LOGGER missing=1'):
            with self.assertRaises(ValueError):logger_groups(rows+[(16.,tail)])

    def test_exact_spacing_and_field_order(self):
        for mutate in (lambda s:' '+s,lambda s:s+' ',lambda s:'\x1b[0m'+s,
                       lambda s:s.replace(' ','  ',1),lambda s:s.replace('found=1','status=1')):
            rows=fixture();rows[-1]=(rows[-1][0],mutate(rows[-1][1]))
            with self.assertRaises(ValueError):logger_groups(rows)

    def test_all_required_network_watchdog_markers_have_exact_grammar(self):
        from m3_integrated_output import validate_grammar
        records=(
            'M3_NET_EXCHANGE trial=1 protocol=2 sequence=3 result=0 monotonic_ms=1000 elapsed_ms=2',
            'M3_NET_RESOURCE monotonic_ms=1000 owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32',
            'M3_WDT_FEED monotonic_ms=1000 sequence=10 completed_ms=1000 network_sequence=1 network_completed_ms=900',
            'M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500 network_freshness_ms=16000',
            'M3_FAULT_STALL monotonic_ms=1000 sequence=10 requested_ms=900 deadline_ms=15900 window_end_ms=60000',
            'M3_NET_FAULT_STALL monotonic_ms=1000 sequence=1 completed_ms=900 requested_ms=900 deadline_ms=15900 window_end_ms=60000',
            'M3_NET_WINDOW_REQUESTED duration_ms=60000',
            'M3_STALL_REQUESTED requested_ms=900 deadline_ms=15900',
            'M3_ARM_REQUESTED',
        )
        for record in records:
            validate_grammar([(0.,record)])
            for changed in (' '+record,record+' ',record.replace(' ','  ',1),
                            record.replace(' ','\t',1),'x '+record):
                if changed==record:continue
                with self.subTest(changed=changed),self.assertRaises(ValueError):
                    validate_grammar([(0.,changed)])

    def test_explicit_pre_reset_roles(self):
        retained_prefix(fixture(),'probe')
        retained_prefix([(0.,' idle      ')],'original')
        for rows,role in [([(0.,'M3_BOOT source=unknown')],'probe'),([(0.,'garbage')],'normal'),
                          ([(0.,'M3_WORK sequence=1')],'probe'),(fixture(),'unknown')]:
            with self.assertRaises(ValueError):retained_prefix(rows,role)

    def test_identified_native_rx_queue_prefix(self):
        line=' rx_q[0]             : STACK: unused 1272 usage 264 / 1536 (17 %); CPU: 0 %'
        for role in ('probe','normal'):
            retained_prefix([(0.,line)],role)
        # This profile has one native RX traffic-class thread. Keep other
        # bracketed names, partial records and terminal escapes unrecognised.
        for name in ('rx_q[1]','rx_q[00]','rx_q[x]','rx_q[0][0]','rx_q[[0]]'):
            with self.subTest(name=name),self.assertRaises(ValueError):
                retained_prefix([(0.,line.replace('rx_q[0]',name))],'probe')
        for changed in (line+' extra',line[:-1], '\x1b[0m'+line):
            with self.subTest(changed=changed),self.assertRaises(ValueError):
                retained_prefix([(0.,changed)],'probe')

    def test_mode_specific_configuration(self):
        root=Path(__file__).resolve().parents[2]
        base=(root/'samples/controller/zephyr/m3-native-output.conf').read_text()
        base+='CONFIG_CRUX_M3_MDIO_COMPLETION_FIX=y\n'
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'.config'
            for role in ('CONFIG_CRUX_M3_NETWORK_PROBE=y\n',
                         'CONFIG_CRUX_M3_INTEGRATED=y\nCONFIG_CRUX_CONTROLLER_M3=y\n'):
                p.write_text(base+role);check_record_output(p)
                for bad in ('CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y\n','CONFIG_LOG_BACKEND_NATIVE_POSIX=y\n',
                            'CONFIG_PRINTK_SYNC=y\n','CONFIG_CRUX_M3_DEFERRED_OUTPUT=y\n'):
                    p.write_text(base+role+bad)
                    with self.assertRaises(ValueError):check_record_output(p)
            p.write_text(base+'CONFIG_CRUX_M3_NETWORK_PROBE=y\nCONFIG_CRUX_M3_INTEGRATED=y\nCONFIG_CRUX_CONTROLLER_M3=y\n')
            with self.assertRaises(ValueError):check_record_output(p)

    def test_native_replay_failure_always_releases_peer(self):
        from test_m3_integrated_capture import IntegratedCaptureControls
        SOURCE='0123456789ab'
        with tempfile.TemporaryDirectory() as d:
            series=IntegratedCaptureControls().series(d);series.source=SOURCE
            series.images['probe']={'binary':'/unused'};series.native_output={'probe':True}
            series.port=Mock();series.wait=Mock(side_effect=lambda predicate,*args:self.assertTrue(predicate()))
            rows=[(9.8,'*** Booting Zephyr fixture ***'),
                  (9.9,f'M3_BOOT source={SOURCE} profile=integrated commands=1 fault_default=off')]+fixture()
            rx=Mock(rows=rows);rx.text.return_value='\n'.join(line for _,line in rows)
            with patch.object(capture,'drain'),patch.object(capture,'Receiver',return_value=rx),patch.object(
                    capture.net,'finish_receiver'),patch.object(capture.net,'release_peer') as release,patch(
                    'm3_uart.exact_event_rows',side_effect=ValueError('partial')),patch.object(
                    capture,'default_state',return_value={'status':'PASS'}) as baseline:
                with self.assertRaises(capture.HardwareStop):series.capture('raw-failure','reset',None)
                baseline.assert_called_once()
                release.assert_called_once()
                self.assertEqual(series.case_end,series.end)
                self.assertTrue((series.directory/'raw-failure/lines.json').exists())

    def test_handoff_rejects_native_config_patch_or_component_drift(self):
        import hashlib
        digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);config=root/'config';config.write_text('CONFIG_NET_TCP=y\nCONFIG_LOG_BUFFER_SIZE=16384\n')
            patch_ids={key:'a'*64 for key in ('upstream_sha256','patch_sha256','generated_sha256')}
            network=dict(profile='network',config=str(config),native_patch=patch_ids,
                         source={'git_commit':'a'*40},west_projects=['pinned'])
            probe=dict(network,source={'git_commit':'b'*40})
            for name in ('review','manifest'):(root/name).write_text(name)
            (root/'result').write_text(json.dumps({'status':'PASS','stages':7}))
            (root/'inputs.json').write_text(json.dumps({'image':network}))
            for name in [f'stage-{n}' for n in range(1,8)]+['final-normal-restoration']:
                (root/name).mkdir();(root/name/'result.json').write_text('{"status":"PASS"}')
            handoff=dict(status='PASS',physical_stages=7,
                         native_identity=capture.native_handoff_identity(network))
            for name in ('review','result','manifest'):
                handoff[name+'_path']=str(root/name);handoff[name+'_sha256']=digest(root/name)
            relatives=('network_client.c','network_client.h','m3-network.overlay','native_logger.c','native_logger.h')
            handoff['component_source_sha256']={
                'samples/controller/zephyr/'+name:hashlib.sha256(b'qualified').hexdigest() for name in relatives}
            handoff['component_source_sha256']['patches/zephyr/nxp-enet-mdio-completion.patch']=hashlib.sha256(b'qualified').hexdigest()
            path=root/'handoff';path.write_text(json.dumps(handoff))
            with patch.object(capture,'verify_manifest',return_value=network),patch.object(
                    capture.subprocess,'check_output',return_value=b'qualified'):
                capture.verify_network_handoff(path,probe)
                altered=root/'altered';altered.write_text(config.read_text().replace('16384','4096'))
                for changed in (dict(probe,config=str(altered)),dict(probe,native_patch=dict(patch_ids,generated_sha256='b'*64))):
                    with self.assertRaises(ValueError):capture.verify_network_handoff(path,changed)
                with patch.object(capture.subprocess,'check_output',side_effect=[b'qualified',b'changed']):
                    with self.assertRaises(ValueError):capture.verify_network_handoff(path,probe)

    def test_normal_role_only_allows_pinned_shell_disable_differences(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);probe=root/'probe';normal=root/'normal'
            base='CONFIG_LOG_BUFFER_SIZE=16384\nCONFIG_NET_TCP=y\n'
            probe.write_text(base+'CONFIG_SHELL=y\nCONFIG_LOG_RUNTIME_FILTERING=y\nCONFIG_LOG_RUNTIME_DEFAULT_LEVEL=0\n# CONFIG_LOG_CMDS is not set\n# CONFIG_MDIO_SHELL is not set\n')
            normal.write_text(base+'# CONFIG_SHELL is not set\n# CONFIG_LOG_RUNTIME_FILTERING is not set\n')
            patch={key:'a'*64 for key in ('upstream_sha256','patch_sha256','generated_sha256')}
            left={'config':str(probe),'native_patch':patch};right={'config':str(normal),'native_patch':patch}
            self.assertEqual(len(capture.normal_native_differences(left,right)),4)
            normal.write_text(normal.read_text().replace('16384','8192'))
            with self.assertRaises(ValueError):capture.normal_native_differences(left,right)


if __name__=='__main__':unittest.main()

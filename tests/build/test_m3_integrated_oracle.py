import sys
from pathlib import Path
import unittest
R=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(R/'tests/target'))
from m3_integrated_oracle import validate_watchdog

SOURCE='fixture-source'
BASE=10000.


def fixture(owner='time'):
    rows=[];peer=[dict(event='peer_ready',monotonic=BASE+3.9,clock='clock_gettime(CLOCK_MONOTONIC)')]
    def add(ms,line):rows.append((BASE+ms/1000,line))
    reset_ms=23000 if owner=='time' else 36700
    last_feed=20900 if owner=='time' else 34500
    fault=20500
    add(0,'*** Booting Zephyr fixture ***')
    add(5,f'M3_BOOT source={SOURCE} profile=integrated commands=1 fault_default=off')
    reset='M3_RESET support_status=0 supported=0x10 cause_status=0 cause=0x1 clear_status=0 remaining_status=0 remaining=0x0'
    add(10,reset)
    add(4000,'M3_NET_WINDOW started_ms=4000 end_ms=64000')
    add(8000,'M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500 network_freshness_ms=16000')
    done=list(range(6000,reset_ms-1000,2500)) if owner=='time' else list(range(6000,18501,2500))
    for sequence,completed in enumerate(done,1):
        for protocol,ms in ((1,completed-1490),(2,completed)):
            add(ms,f'M3_NET_EXCHANGE trial=1 protocol={protocol} sequence={sequence} result=0 monotonic_ms={ms} elapsed_ms=10')
            add(ms+1,'M3_NET_CLEANUP result=0 expected_contexts=0 observed_contexts=0 elapsed_ms=100 restart_required=0')
            add(ms+2,f'M3_NET_RESOURCE monotonic_ms={ms} owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32')
            peer.append(dict(event='exchange',protocol='tcp' if protocol==1 else 'udp',trial=1,sequence=sequence,
                mode='echo',received_bytes=32,peer=['192.168.77.2',123],response='sent',monotonic=BASE+ms/1000))
        add(completed+3,f'M3_NET_WORK sequence={sequence} completed_ms={completed} result=0')
    for ms in range(8000,last_feed+1,100):
        actual=min(ms,20400) if owner=='time' else ms
        net=max(t for t in done if t<=ms)
        netseq=done.index(net)+1
        add(ms+5,f'M3_WDT_FEED monotonic_ms={ms} sequence={actual//100+1} completed_ms={actual} network_sequence={netseq} network_completed_ms={net}')
    add(fault-10,('M3_STALL_REQUESTED' if owner=='time' else 'M3_NET_STALL_REQUESTED')+' requested_ms=20490 deadline_ms=35490')
    if owner=='time':
        add(fault,'M3_FAULT_STALL monotonic_ms=20500 sequence=205 requested_ms=20490 deadline_ms=35490 window_end_ms=64000')
    else:
        add(fault,'M3_NET_FAULT_STALL monotonic_ms=20500 sequence=6 completed_ms=18500 requested_ms=20490 deadline_ms=35490 window_end_ms=64000')
    add(last_feed+100,f'M3_HEALTH_STALE monotonic_ms={last_feed+100} sequence=205 network_sequence=6 network_completed_ms=18500 window_end_ms=64000')
    for ms in range(0,reset_ms,1000):
        healthy=int(6000<=ms<=last_feed)
        seq=min(ms,20400)//100+1 if owner=='time' else ms//100+1
        netdone=max([t for t in done if t<=ms] or [-1])
        netseq=done.index(netdone)+1 if netdone!=-1 else 0
        add(ms+10,f'M3_OBSERVER_ALIVE monotonic_ms={ms} healthy={healthy} sequence={seq} readiness={healthy} network_sequence={netseq} network_completed_ms={netdone} network_active={int(ms>=4000)} network_status=0 window_end_ms=64000')
        add(ms+11,'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=4096 main_unused=2000 network_status=0 network_object=4160 network_usable=4096 network_unused=2500')
        if owner=='network' or ms<fault:
            add(ms+12,f'M3_WORK sequence={ms//100+1} monotonic_ms={ms} time=-4 pressed=0 device=0')
    add(reset_ms,'*** Booting Zephyr fixture ***')
    add(reset_ms+5,f'M3_BOOT source={SOURCE} profile=integrated commands=1 fault_default=off')
    add(reset_ms+10,reset.replace('cause=0x1 ','cause=0x10 '))
    for ms in range(0,4001,1000):
        add(reset_ms+ms+20,f'M3_OBSERVER_ALIVE monotonic_ms={ms} healthy=0 sequence={ms//100+1} readiness=0 network_sequence=0 network_completed_ms=-1 network_active=0 network_status=0 window_end_ms=-1')
        add(reset_ms+ms+21,'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=4096 main_unused=2000 network_status=0 network_object=4160 network_usable=4096 network_unused=2500')
        add(reset_ms+ms+22,f'M3_WORK sequence={ms//100+1} monotonic_ms={ms} time=-4 pressed=0 device=0')
    peer.append(dict(event='peer_closed',exchanges=len(peer)-1,monotonic=BASE+(reset_ms+4500)/1000))
    return sorted(rows),peer


class IntegratedVerdicts(unittest.TestCase):
    def test_actual_declared_owner_paths(self):
        for owner in ('time','network'):
            with self.subTest(owner=owner):self.assertEqual(validate_watchdog(*fixture(owner),SOURCE,owner)['status'],'PASS')

    def test_wrong_configuration_freshness_stacks_reset_and_fault_budget(self):
        for owner in ('time','network'):
            for old,new in [('timeout_ms=2000','timeout_ms=3000'),('main_unused=2000','main_unused=1000'),
                            ('remaining=0x0','remaining=0x10'),('window_end_ms=64000','window_end_ms=30000'),
                            ('deadline_ms=35490','deadline_ms=20500'),('cause=0x10 ','cause=0x1 ')]:
                rows,peer=fixture(owner)
                with self.subTest(owner=owner,old=old),self.assertRaises(ValueError):
                    validate_watchdog([(t,s.replace(old,new)) for t,s in rows],peer,SOURCE,owner)

    def test_last_valid_network_clock_is_not_stall_marker(self):
        rows,peer=fixture('network')
        rows=[(t,s.replace('completed_ms=18500 requested_ms','completed_ms=18000 requested_ms')) for t,s in rows]
        with self.assertRaises(ValueError):validate_watchdog(rows,peer,SOURCE,'network')





def functional_fixture():
    rows=[];peer=[dict(event='peer_ready',monotonic=BASE+3.9,clock='clock_gettime(CLOCK_MONOTONIC)')]
    def add(ms,line):rows.append((BASE+ms/1000,line))
    add(0,'*** Booting Zephyr fixture ***');add(5,f'M3_BOOT source={SOURCE} profile=integrated commands=1 fault_default=off')
    add(10,'M3_RESET support_status=0 supported=0x10 cause_status=0 cause=0x1 clear_status=0 remaining_status=0 remaining=0x0')
    add(4000,'M3_NET_WINDOW started_ms=4000 end_ms=64000')
    for seq,end in enumerate(range(6000,51001,2500),1):
        for p,ms in ((1,end-1490),(2,end)):
            add(ms,f'M3_NET_EXCHANGE trial=1 protocol={p} sequence={seq} result=0 monotonic_ms={ms} elapsed_ms=10')
            add(ms+1,'M3_NET_CLEANUP result=0 expected_contexts=0 observed_contexts=0 elapsed_ms=100 restart_required=0')
            peer.append(dict(event='exchange',protocol='tcp' if p==1 else 'udp',trial=1,sequence=seq,
                mode='echo',received_bytes=32,peer=['192.168.77.2',123],response='sent',monotonic=BASE+ms/1000))
        add(end+3,f'M3_NET_WORK sequence={seq} completed_ms={end} result=0')
    add(64000,'M3_NET_WINDOW_STOP monotonic_ms=64000 result=0')
    for ms in range(0,67000,1000):
        healthy=int(6000<=ms<64000)
        add(ms+10,f'M3_OBSERVER_ALIVE monotonic_ms={ms} healthy={healthy} sequence={ms//100+1} readiness={healthy} network_sequence=1 network_completed_ms=6000 network_active={int(4000<=ms<64000)} network_status=0 window_end_ms=64000')
        add(ms+11,'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=4096 main_unused=2000 network_status=0 network_object=4160 network_usable=4096 network_unused=2500')
        pressed=int(any(11000+8000*g<=ms<15000+8000*g for g in range(5)))
        add(ms+12,f'M3_WORK sequence={ms//100+1} monotonic_ms={ms} time=-4 pressed={pressed} device=0')
        if ms<4000 or ms>=64000:
            add(ms+13,f'M3_NET_RESOURCE monotonic_ms={ms} owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32')
            add(ms+14,f'M3_NET_STATE monotonic_ms={ms} link=1 address=1 service_ready={int(ms>=64000)}')
    peer.append(dict(event='peer_closed',exchanges=len(peer)-1,monotonic=BASE+67))
    return sorted(rows),peer


class IntegratedFunctionalVerdicts(unittest.TestCase):
    def test_full_window_with_and_without_gpio_stimulus(self):
        from m3_integrated_oracle import validate_functional
        rows,peer=functional_fixture()
        self.assertEqual(validate_functional(rows,peer,SOURCE)['status'],'PASS')
        self.assertEqual(validate_functional(rows,peer,SOURCE,(BASE+9,BASE+54))['gpio_groups'],5)

    def test_missing_stimulus_or_postwindow_native_leak(self):
        from m3_integrated_oracle import validate_functional
        rows,peer=functional_fixture()
        with self.assertRaises(ValueError):
            validate_functional([(t,s.replace('pressed=1','pressed=0')) for t,s in rows],peer,SOURCE,(BASE+9,BASE+54))
        rows=[(t,s.replace('contexts=0','contexts=1') if t>=BASE+64 else s) for t,s in rows]
        with self.assertRaises(ValueError):validate_functional(rows,peer,SOURCE)


def loss_fixture(fenced=False):
    from m3_integrated_oracle import records
    rows,peer=functional_fixture()
    kept=[]
    stop_ms=20600 if fenced else 15700
    for received,line in rows:
        target=(received-BASE)*1000
        if target>stop_ms+3500 or 'M3_NET_WINDOW_STOP ' in line:continue
        if 'M3_NET_EXCHANGE ' in line and records([(0,line)],'M3_NET_EXCHANGE')[0]['sequence']>4:continue
        if 'M3_NET_WORK ' in line and records([(0,line)],'M3_NET_WORK')[0]['sequence']>4:continue
        if 'M3_NET_CLEANUP ' in line and target>=14500:continue
        if 'M3_OBSERVER_ALIVE ' in line and target>=16000:
            line=line.replace('healthy=1','healthy=0').replace('readiness=1','readiness=0')
        kept.append((received,line))
    def add(ms,line):kept.append((BASE+ms/1000,line))
    add(15500,'M3_NET_EXCHANGE trial=1 protocol=1 sequence=5 result=-115 monotonic_ms=15500 elapsed_ms=100')
    add(13400,'M3_NET_STATE monotonic_ms=13400 link=1 address=1 service_ready=1')
    add(15600,'M3_NET_STATE monotonic_ms=15600 link=0 address=1 service_ready=0')
    add(stop_ms-10,('M3_NET_CLEANUP result=-116 expected_contexts=0 observed_contexts=1 elapsed_ms=5000 restart_required=1'
                   if fenced else 'M3_NET_CLEANUP result=0 expected_contexts=0 observed_contexts=0 elapsed_ms=10 restart_required=0'))
    add(stop_ms,f'M3_NET_WINDOW_STOP monotonic_ms={stop_ms} result={-116 if fenced else -115}')
    for ms in (stop_ms,stop_ms+1000,stop_ms+2000):
        add(ms+10,f'M3_NET_RESOURCE monotonic_ms={ms} owned=0 contexts={int(fenced)} rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32')
        add(ms+11,f'M3_NET_STATE monotonic_ms={ms} link=0 address=1 service_ready=0')
    frames=[r for r in peer if r['event']=='exchange' and r['sequence']<=4]
    peer=[peer[0],*frames,dict(event='peer_closed',exchanges=len(frames),monotonic=BASE+(stop_ms+4000)/1000)]
    return sorted(kept),peer


class IntegratedLossVerdicts(unittest.TestCase):
    def test_failure_before_acknowledgement_and_explicit_restart_fence(self):
        from m3_integrated_oracle import validate_link_loss
        abi=dict(EIO=5,ENETDOWN=115,ENETUNREACH=114,ECONNRESET=104,ENOTCONN=128,ETIMEDOUT=116,ECONNREFUSED=111,EHOSTUNREACH=118)
        for fenced in (False,True):
            rows,peer=loss_fixture(fenced)
            result=validate_link_loss(rows,peer,SOURCE,BASE+14,BASE+16,BASE+16.1,abi)
            self.assertEqual(result['status'],'PASS')
            self.assertEqual(result['in_place_cleanup_failed'],fenced)

    def test_expiry_wrong_errno_growth_fence_peer_and_late_ack_fail(self):
        from m3_integrated_oracle import validate_link_loss
        abi=dict(EIO=5,ENETDOWN=115,ENETUNREACH=114,ECONNRESET=104,ENOTCONN=128,ETIMEDOUT=116,ECONNREFUSED=111,EHOSTUNREACH=118)
        rows,peer=loss_fixture(True)
        for old,new in [('result=-115','result=-110'),('observed_contexts=1','observed_contexts=2'),
                        ('expected_contexts=0','expected_contexts=1'),('contexts=1 rx_pkt','contexts=2 rx_pkt')]:
            with self.subTest(old=old),self.assertRaises(ValueError):
                validate_link_loss([(t,line.replace(old,new)) for t,line in rows],peer,SOURCE,BASE+14,BASE+16,BASE+16.1,abi)
        with self.assertRaises(ValueError):validate_link_loss(rows,peer,SOURCE,BASE+14,BASE+45,BASE+45.1,abi)
        changed=[dict(r) for r in peer];changed[-1]['monotonic']=BASE+15
        with self.assertRaises(ValueError):validate_link_loss(rows,changed,SOURCE,BASE+14,BASE+16,BASE+16.1,abi)
        extra=(BASE+21,'M3_NET_EXCHANGE trial=1 protocol=1 sequence=6 result=-115 monotonic_ms=21000 elapsed_ms=0')
        with self.assertRaises(ValueError):validate_link_loss(sorted(rows+[extra]),peer,SOURCE,BASE+14,BASE+16,BASE+16.1,abi)



class IntegratedPairingControls(unittest.TestCase):
    def test_stacks_must_pair_with_each_observer(self):
        from m3_integrated_oracle import stacks
        rows,_=fixture()
        missing=next(i for i,(_,line) in enumerate(rows) if 'M3_STACK ' in line)
        with self.assertRaises(ValueError):stacks(rows[:missing]+rows[missing+1:])
        changed=list(rows);t,line=changed[missing];changed[missing]=(t+.3,line)
        with self.assertRaises(ValueError):stacks(changed)
    def test_idle_state_missing_or_duplicate_fails(self):
        from m3_integrated_oracle import idle_resources
        rows,_=functional_fixture();rows=[(t,s) for t,s in rows if t<BASE+4]
        index=next(i for i,(_,line) in enumerate(rows) if 'M3_NET_STATE ' in line)
        for altered in (rows[:index]+rows[index+1:],rows[:index]+[rows[index]]+rows[index:]):
            with self.assertRaises(ValueError):idle_resources(altered)
    def test_known_preinstruction_failure_cannot_hide_behind_old_health(self):
        from m3_integrated_oracle import validate_link_loss
        abi=dict(ENETDOWN=115,ENETUNREACH=114,ECONNRESET=104,ENOTCONN=128,ETIMEDOUT=116,ECONNREFUSED=111,EHOSTUNREACH=118)
        for addition in ('M3_NET_STATE monotonic_ms=13900 link=0 address=1 service_ready=0',
                         'M3_OBSERVER_ALIVE monotonic_ms=13900 healthy=0 readiness=0 network_active=0'):
            rows,peer=loss_fixture()
            with self.assertRaises(ValueError):validate_link_loss(sorted(rows+[(BASE+13.9,addition)]),peer,SOURCE,BASE+14,BASE+16,BASE+16.1,abi)



    def test_arm_tail_and_functional_run_are_contiguous(self):
        from m3_integrated_oracle import validate_functional
        rows,peer=fixture()
        for changed in (
            [(t,line) for t,line in rows if not (BASE+6<t<BASE+8 and ('M3_OBSERVER_ALIVE ' in line or 'M3_STACK ' in line))],
            [(t,line.replace('healthy=1','healthy=0').replace('readiness=1','readiness=0')) if 'M3_OBSERVER_ALIVE ' in line and BASE+7<t<BASE+8 else (t,line) for t,line in rows]):
            with self.assertRaises(ValueError):validate_watchdog(changed,peer,SOURCE,'time')
        rows,peer=functional_fixture()
        changed=[(t,line.replace('healthy=1','healthy=0').replace('readiness=1','readiness=0')) if 'M3_OBSERVER_ALIVE ' in line and int(t-BASE)%4==0 else (t,line) for t,line in rows]
        with self.assertRaises(ValueError):validate_functional(changed,peer,SOURCE)

if __name__=='__main__':unittest.main()

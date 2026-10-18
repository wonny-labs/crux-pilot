import copy
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
from m3_network_oracle import validate_stage as target_validate
LINUX = dict(EIO=5, ENETDOWN=100, ENETUNREACH=101, ECONNRESET=104, ENOTCONN=107,
             ETIMEDOUT=110, ECONNREFUSED=111, EHOSTUNREACH=113)
PICOLIBC = dict(EIO=5, ENETDOWN=115, ENETUNREACH=114, ECONNRESET=104, ENOTCONN=128,
                ETIMEDOUT=116, ECONNREFUSED=111, EHOSTUNREACH=118)


def validate_stage(*args):
    return target_validate(*args, errno_abi=LINUX)


def fixture(mode='echo'):
    rows = [(1., 'M3_NET_STATE monotonic_ms=1000 link=1 address=1 service_ready=0'),
            (1., 'M3_NET_ROUND_BUDGET trial=1 started_ms=1000 deadline_ms=151000')]
    peer = [dict(event='peer_ready', monotonic=0., clock='clock_gettime(CLOCK_MONOTONIC)')]
    for p in (1, 2):
        for s in range(1, 11):
            t = len(peer) + 1
            result = 0 if mode == 'echo' or (mode == 'close' and p == 2) else -110 if mode == 'silence' else -104
            rows += [(float(t), f'M3_NET_EXCHANGE trial=1 protocol={p} sequence={s} result={result} monotonic_ms={t*1000} elapsed_ms=100'),
                     (float(t), 'M3_NET_CLEANUP result=0 expected_contexts=0 observed_contexts=0 elapsed_ms=100 restart_required=0')]
            peer.append(dict(event='exchange', protocol='tcp' if p == 1 else 'udp', trial=1,
                             sequence=s, mode=mode, received_bytes=32, peer=['192.168.77.2', 1],
                             response='sent' if result == 0 else 'withheld' if mode == 'silence' else 'closed', monotonic=float(t)))
    ready = int(mode == 'echo')
    rows.append((23., f'M3_NET_ROUND trial=1 result={0 if ready else -5} monotonic_ms=23000'))
    for t in (24, 25, 26):
        rows += [(float(t), f'M3_NET_RESOURCE monotonic_ms={t*1000} owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32'),
                 (float(t), f'M3_NET_STATE monotonic_ms={t*1000} link=1 address=1 service_ready={ready}'),
                 (float(t), 'M3_NET_STACK status=0 usable=4096 unused=2000')]
    peer.append(dict(event='peer_closed', exchanges=20, monotonic=27.))
    return rows, peer


class NetworkVerdicts(unittest.TestCase):
    def test_declared_modes(self):
        for mode in ('echo', 'silence', 'close'):
            self.assertEqual(validate_stage(*fixture(mode), 1, mode)['status'], 'PASS')

    def test_missing_peer_frames_wrong_response_and_lifecycle(self):
        for change in ('missing', 'identity', 'response', 'early_expiry', 'reject'):
            rows, peer = fixture()
            if change == 'missing': del peer[1]
            if change == 'identity': peer[1]['trial'] = 2
            if change == 'response': peer[1]['response'] = 'withheld'
            if change == 'early_expiry': peer[-1]['monotonic'] = 2.
            if change == 'reject': peer[1]['event'] = 'rejected'
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_stage(rows, peer, 1, 'echo')

    def test_wrong_target_result_leak_stack_deadline_and_duplicate(self):
        for old, new in [('contexts=0', 'contexts=1'), ('unused=2000', 'unused=1000'),
                         ('deadline_ms=151000', 'deadline_ms=152000'),
                         ('elapsed_ms=100', 'elapsed_ms=6000'), ('result=0 monotonic_ms=23000', 'result=-5 monotonic_ms=23000')]:
            rows, peer = fixture()
            rows = [(t, s.replace(old, new)) for t, s in rows]
            with self.subTest(old=old), self.assertRaises(ValueError):
                validate_stage(rows, peer, 1, 'echo')
        rows, peer = fixture()
        rows.insert(2, copy.deepcopy(rows[2]))
        with self.assertRaises(ValueError): validate_stage(rows, peer, 1, 'echo')

    def test_link_down_clean_and_fenced_paths(self):
        rows, peer = fixture('silence')
        rows = [(t, s.replace('link=1', 'link=0')) for t,s in rows]
        peer = [peer[0], dict(event='peer_closed', exchanges=0, monotonic=27.)]
        self.assertEqual(validate_stage(rows,peer,1,'echo',True)['status'],'PASS')
        cut = next(i for i,(_,s) in enumerate(rows) if 'M3_NET_CLEANUP ' in s)
        fenced = rows[:cut] + [(rows[cut][0], 'M3_NET_CLEANUP result=-110 expected_contexts=0 observed_contexts=1 elapsed_ms=5000 restart_required=1'),
            (23., 'M3_NET_ROUND trial=1 result=-110 monotonic_ms=23000')] + rows[-9:]
        fenced = [(t,s.replace('contexts=0 rx_pkt', 'contexts=1 rx_pkt')) for t,s in fenced]
        self.assertEqual(validate_stage(fenced,peer,1,'echo',True)['resources']['status'], 'IN_PLACE_CLEANUP_FAILED')
        for bad in (fenced[:cut+1]+[rows[cut+1]]+fenced[cut+1:],
                    [(t,s.replace('observed_contexts=1','observed_contexts=2')) for t,s in fenced]):
            with self.assertRaises(ValueError): validate_stage(bad,peer,1,'echo',True)

    def test_native_baseline_and_impossible_timelines_fail(self):
        for old,new in [('expected_contexts=0','expected_contexts=1'),
                        ('observed_contexts=0','observed_contexts=1'),
                        ('monotonic_ms=2000 elapsed_ms=100','monotonic_ms=999999 elapsed_ms=100'),
                        ('monotonic_ms=3000 elapsed_ms=100','monotonic_ms=1000 elapsed_ms=100')]:
            rows,peer=fixture()
            with self.subTest(old=old), self.assertRaises(ValueError):
                validate_stage([(t,s.replace(old,new)) for t,s in rows],peer,1,'echo')

    def test_target_abi_is_explicit_and_linux_union_is_not_accepted(self):
        rows, peer = fixture('silence')
        target = [(t, s.replace('result=-110', 'result=-116')) for t, s in rows]
        self.assertEqual(target_validate(target, peer, 1, 'silence', errno_abi=PICOLIBC)['status'], 'PASS')
        with self.assertRaises(ValueError): target_validate(target, peer, 1, 'silence', errno_abi=LINUX)
        rows = [(t,s.replace('link=1','link=0')) for t,s in rows]
        peer = [peer[0], dict(event='peer_closed',exchanges=0,monotonic=27.)]
        # Picolibc110 means ESHUTDOWN, not timeout; never accept a union of ABI numbers.
        with self.assertRaises(ValueError): target_validate(rows, peer, 1, 'echo', True, errno_abi=PICOLIBC)


if __name__ == '__main__': unittest.main()

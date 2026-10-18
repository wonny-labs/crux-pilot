import json
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
from m3_network_output import validate_native_records, logger_pairs, logger_checkpoint, acknowledged_rows
from m3_uart import exact_event_rows


def fixture():
    rows=[]
    for second in (1,2,3,4):
        n=second*1000
        rows.extend([(float(second),f'M3_NET_RESOURCE monotonic_ms={n} owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32'),
            (second+.01,f'M3_NET_STATE monotonic_ms={n} link=1 address=1 service_ready=0'),
            (second+.02,'M3_NET_STACK status=0 usable=4096 unused=2000'),
            (second+.03,f'M3_NET_LOGGER monotonic_ms={n+1} queue_snapshot_ms={n} found=1 status=0 configured=2048 usable=2048 unused=1560 priority=14 queued=0')])
    return rows


class NativeOutput(unittest.TestCase):
    def test_first_three_consecutive_idle_observations(self):
        rows=fixture();validate_native_records(rows)
        self.assertEqual(logger_checkpoint(rows,.5),dict(samples=3,min_unused=1560,queue=0))
        self.assertEqual(len(logger_pairs(rows)),4)
    def test_no_favourable_resampling_after_initial_queued_sample(self):
        rows=fixture();rows[3]=(rows[3][0],rows[3][1].replace('queued=0','queued=1'))
        with self.assertRaises(ValueError):logger_checkpoint(rows,.5)
    def test_identity_stack_priority_and_snapshot_bounds(self):
        for old,new in [('found=1','found=2'),('unused=1560','unused=1023'),('usable=2048','usable=2112'),
                        ('priority=14','priority=0'),('queued=0','queued=4097'),
                        ('queue_snapshot_ms=1000','queue_snapshot_ms=1251'),
                        ('monotonic_ms=1001','monotonic_ms=1251')]:
            rows=fixture();rows[3]=(rows[3][0],rows[3][1].replace(old,new))
            with self.subTest(old=old),self.assertRaises(ValueError):logger_pairs(rows)
    def test_queue_during_work_is_observation_not_immediate_failure(self):
        rows=fixture();rows[3]=(rows[3][0],rows[3][1].replace('queued=0','queued=3'))
        self.assertEqual(len(logger_pairs(rows)),4)
    def test_group_order_duplicates_missing_and_late_pair_rejected(self):
        rows=fixture();swapped=list(rows);swapped[1],swapped[2]=swapped[2],swapped[1]
        late=list(rows);late[3]=(.3+late[3][0],late[3][1])
        for changed in (swapped,rows[:1]+rows[2:],rows[:2]+[rows[1]]+rows[2:],late):
            with self.assertRaises(ValueError):logger_pairs(changed)
    def test_malformed_spacing_prefix_fields_and_loss_rejected(self):
        for change in (lambda s:' '+s,lambda s:s+' ',lambda s:s.replace(' ','  ',1),
                       lambda s:s.replace(' ','\t',1),lambda s:s.replace('found=1','status=1'),
                       lambda s:'\x1b[0m'+s):
            rows=fixture();rows[3]=(rows[3][0],change(rows[3][1]))
            with self.assertRaises(ValueError):validate_native_records(rows)
        for extra in ('--- 95 messages dropped ---','M3_MDIO_FIRST_FAILURE valid=1','M3_OUTPUT_START window_ms=15000'):
            with self.assertRaises(ValueError):validate_native_records(fixture()+[(5.,extra)])
    def test_ack_publication_and_boundary_replays(self):
        ack=(10.,'M3_NET_REQUESTED');budget=(10.,'M3_NET_ROUND_BUDGET trial=1 started_ms=1000 deadline_ms=151000')
        self.assertEqual(acknowledged_rows([ack,budget],9.9),[ack,budget])
        for rows,written in (([budget,ack],9.9),([ack,ack,budget],9.9),([ack,budget],10.1),([ack,budget],8.9)):
            with self.assertRaises(ValueError):acknowledged_rows(rows,written)
    def test_missing_later_logger_and_pre_ack_invalid_values(self):
        rows=fixture()[:-1]
        with self.assertRaises(ValueError):logger_pairs(rows)
        for old,new in [('found=1','found=2'),('unused=1560','unused=1023')]:
            rows=fixture();rows[3]=(rows[3][0],rows[3][1].replace(old,new))
            rows.insert(4,(1.1,'M3_NET_REQUESTED'))
            with self.assertRaises(ValueError):logger_pairs(rows,allow_prefix=True)
    def test_only_initial_partial_group_and_exact_ack_interleaving_allowed(self):
        rows=fixture();rows.insert(2,(1.015,'M3_NET_REQUESTED'))
        self.assertEqual(len(logger_pairs(rows)),4)
        self.assertEqual(len(logger_pairs(fixture()[2:],allow_prefix=True)),3)
        for changed in (fixture()[2:], fixture()[2:]+[(5.,fixture()[3][1])],
                        [(t,s.replace('owned=0','owned=-1')) for t,s in fixture()],
                        fixture()[:2]+[(1.015,'M3_NET_ROUND trial=1 result=0 monotonic_ms=1000')]+fixture()[2:]):
            with self.assertRaises(ValueError):logger_pairs(changed)
        badprefix=fixture()[2:];badprefix[1]=(badprefix[1][0],badprefix[1][1].replace('found=1','found=2'))
        with self.assertRaises(ValueError):logger_pairs(badprefix,allow_prefix=True)
        later_missing=fixture()[:4]+fixture()[5:]
        with self.assertRaises(ValueError):logger_pairs(later_missing,allow_prefix=True)

    def test_full_stage_oracle_checks_pre_ack_resources(self):
        from test_m3_network_oracle import fixture as stage_fixture, LINUX
        from m3_network_oracle import validate_stage
        rows,peer=stage_fixture()
        rows.insert(0,(1.,'M3_NET_RESOURCE monotonic_ms=1000 owned=0 contexts=0 rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32'))
        expanded=[]
        for t,line in rows:
            expanded.append((t,line))
            if line.startswith('M3_NET_STACK '):
                n=int(t*1000)
                expanded.append((t,f'M3_NET_LOGGER monotonic_ms={n} queue_snapshot_ms={n} found=1 status=0 configured=2048 usable=2048 unused=1560 priority=14 queued=0'))
        prefix=[(t-5,line) for t,line in fixture()[:4]]
        capture=prefix+[(.5,'M3_NET_REQUESTED')]+expanded
        self.assertEqual(validate_stage(expanded,peer,1,'echo',errno_abi=LINUX,native_output=True,native_capture=capture)['status'],'PASS')
        for old,new in [('found=1','found=2'),('unused=1560','unused=1023')]:
            bad=list(capture);bad[3]=(bad[3][0],bad[3][1].replace(old,new))
            with self.assertRaises(ValueError):validate_stage(expanded,peer,1,'echo',errno_abi=LINUX,native_output=True,native_capture=bad)

    def test_actual_request_publishes_ack_before_work(self):
        source=(Path(__file__).resolve().parents[2]/'samples/controller/zephyr/network_probe.c').read_text()
        function=source[source.index('static int request_round('):source.index('SHELL_CMD_REGISTER(')]
        model=r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define ARG_UNUSED(x) (void)(x)
struct shell {int unused;};
static int requested, enqueued;
static bool atomic_cas(int *value,int old,int next) {
    if (*value != old) return false;
    *value=next; return true;
}
static void atomic_set(int *value,int next) {
    assert(next == 1 && enqueued == 1 && *value == -1);
    *value=next;
}
static int printk(const char *line) {
    assert(!atomic_cas(&requested,1,2));
    assert(strcmp(line,"M3_NET_REQUESTED\n") == 0);
    enqueued++; return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);p=root/'ack.c'
            p.write_text(model+function+'int main(void) {assert(request_round(NULL,0,NULL)==0); assert(requested==1 && enqueued==1); return 0;}')
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror',str(p),'-o',str(root/'ack')],check=True)
            subprocess.run([str(root/'ack')],check=True)

    def test_exact_raw_fragment_timestamps_and_incomplete_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);chunks=[(1.,b' first\r'),(1.02,b'\nsecond \r\n')]
            raw=b''.join(c for _,c in chunks)
            (root/'uart-rx.bin').write_bytes(raw)
            (root/'receive.jsonl').write_text(''.join(json.dumps(dict(monotonic=t,hex=c.hex()))+'\n' for t,c in chunks))
            self.assertEqual(exact_event_rows(root),[(1.02,' first'),(1.02,'second ')])
            (root/'uart-rx.bin').write_bytes(raw+b'x')
            with self.assertRaises(ValueError):exact_event_rows(root)


if __name__=='__main__':unittest.main()

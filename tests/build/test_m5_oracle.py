"""Independent hand-constructed records exercise acceptance, not source spelling."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import m5_network_oracle as oracle

ABI={'ETIMEDOUT':116,'ECONNRESET':104,'ENETDOWN':115,'ENETUNREACH':114}
def records():
    rows=[]
    for udp,start,end in [(0,10,40),(1,2050,2060)]:
        rows.extend([dict(event='BEGIN',token=7,seq=1,round=0,udp=udp,at=start),dict(event='END',token=7,seq=1,round=0,udp=udp,at=end,result=0,entered=start,functional=end,close_error=0,peak=1,owned=0)])
        for at in [end+10,end+1010,end+2010]:
            rows.append(dict(event='IDLE',token=7,seq=1,checkpoint=udp,at=at,contexts=0,rx_pkt=16,tx_pkt=16,rx_buf=32,tx_buf=32,main_free=1600,observer_free=800))
    rows.append(dict(event='DONE',token=7,seq=1,at=4100))
    return rows

class Oracle(unittest.TestCase):
    def test_completed_correlated_case_is_accepted(self):
        self.assertTrue(oracle.validate_command(records(),7,1,1,'echo',ABI))
    def test_missing_duplicate_or_foreign_epoch_is_rejected(self):
        for mutate in [lambda r:r.pop(3),lambda r:r.append(r[0].copy()),lambda r:r[0].update(token=8),lambda r:r[-1].update(seq=2),lambda r:r[1].update(at=9),lambda r:r[2].update(contexts=1),lambda r:r[2].update(main_free=1023),lambda r:r[3].update(at=60)]:
            rows=records();mutate(rows)
            with self.subTest(rows=rows),self.assertRaises(ValueError):oracle.validate_command(rows,7,1,1,'echo',ABI)
    def test_late_or_unwitnessed_link_loss_cannot_pass(self):
        rows=records();rows[1].update(result=-116);rows[6].update(result=-115)
        for at in [5,41]:
            data=rows[:]+[dict(event='LINK',token=7,seq=1,at=at,link=1,admin=1,address=1,ipv4=3232255234)]
            with self.subTest(at=at),self.assertRaises(ValueError):oracle.validate_command(data,7,1,1,'loss',ABI)
        rows.insert(1,dict(event='LINK',token=7,seq=1,at=20,link=1,admin=1,address=1,ipv4=3232255234))
        self.assertTrue(oracle.validate_command(rows,7,1,1,'loss',ABI))
    def test_latency_review_is_separate_from_functional_budget(self):
        rows=records()
        # Functional work ended in30ms; a single close took3seconds.
        rows[1].update(at=3040,functional=40)
        for row in rows[2:]:
            row['at']+=3000
            if row['event']=='END':
                row['entered']+=3000;row['functional']+=3000
        result=oracle.validate_command(rows,7,1,1,'echo',ABI)
        self.assertEqual(result['status'],'REVIEW_REQUIRED')
        self.assertEqual(result['max_call_ms'],3030)
    def test_link_fields_include_numeric_ipv4_name(self):
        line = 'M5 LINK token=7 seq=1 at=20 link=1 admin=1 address=1 ipv4=3232255234'
        self.assertEqual(oracle.parse_record(line)['ipv4'], 3232255234)

    def test_malformed_record_or_unknown_failure_is_not_ignored(self):
        for line in ['M5 nonsense','M5 END token=7 token=8','M5_END x','M5 FAIL token=7 seq=1 at=1 code=2']:
            with self.subTest(line=line),self.assertRaises(ValueError):oracle.parse_record(line)
        self.assertEqual(oracle.parse_record('M5 DONE token=7 seq=1 at=10'),dict(event='DONE',token=7,seq=1,at=10))

if __name__=='__main__':unittest.main()

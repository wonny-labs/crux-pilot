"""Liveness is host supervision, not a cancellation or socket contract."""
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
from m5_network_oracle import Watch,Inconclusive
class WatchTests(unittest.TestCase):
    def test_missing_begin_and_hung_call_are_bounded(self):
        for events in [[],[('BEGIN',5)],[('LINK',9),('IDLE',9.9)]]:
            watch=Watch(0,150,1800)
            for event,at in events:watch.feed(event,at)
            with self.subTest(events=events),self.assertRaises(Inconclusive):watch.check(15 if events==[('BEGIN',5)] else 10)
    def test_command_and_series_deadlines_are_never_renewed(self):
        watch=Watch(0,12,11)
        watch.feed('BEGIN',5);watch.feed('END',9)
        with self.assertRaises(Inconclusive):watch.check(11)
    def test_end_without_begin_and_nested_begin_are_rejected(self):
        watch=Watch(0,150,1800)
        with self.assertRaises(ValueError):watch.feed('END',1)
        watch.feed('BEGIN',1)
        with self.assertRaises(ValueError):watch.feed('BEGIN',2)
if __name__=='__main__':unittest.main()

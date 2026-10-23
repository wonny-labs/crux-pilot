from pathlib import Path
import copy,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import m5_network_oracle as oracle

def peer_rows():
    rows=[dict(event='ready',token=7,phase=2,mode='echo',address='192.168.77.1',tcp_port=42420,udp_port=42421,monotonic=1,clock='clock_gettime(CLOCK_MONOTONIC)')]
    for protocol,number,at in [('tcp','1',2),('udp','2',4)]:
        frame='4d354e31000000070002000'+number+'00000001637275782d6d352d6563686f2d763100'
        rows.extend([dict(event='request',token=7,phase=2,protocol=protocol,transaction=1,request_hex=frame,monotonic=at,clock='clock_gettime(CLOCK_MONOTONIC)'),dict(event='exchange',token=7,phase=2,protocol=protocol,transaction=1,request_hex=frame,response_hex=frame,monotonic=at+1,clock='clock_gettime(CLOCK_MONOTONIC)')])
    return rows
class PeerEvidence(unittest.TestCase):
    def test_wire_and_both_end_records_are_correlated(self):self.assertTrue(oracle.validate_peer(peer_rows(),7,2,1,'echo'))
    def test_missing_duplicate_wrong_nonce_frame_and_response_rejected(self):
        for mutate in [lambda r:r.pop(),lambda r:r.append(r[1].copy()),lambda r:r[1].update(token=8),lambda r:r[1].update(request_hex='00'*32),lambda r:r[2].update(response_hex=''),lambda r:r[0].update(address='0.0.0.0'),lambda r:r[1].update(monotonic=0)]:
            rows=copy.deepcopy(peer_rows());mutate(rows)
            with self.subTest(rows=rows),self.assertRaises(ValueError):oracle.validate_peer(rows,7,2,1,'echo')
if __name__=='__main__':unittest.main()

"""M5 peer tests reject cross-epoch/M3/duplicate wire evidence and run real loopback."""
from pathlib import Path
import io,json,socket,struct,sys,threading,time,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import m5_network_peer as peer
FRAME=struct.pack('!4sIHHI16s',b'M5N1',7,2,1,1,b'crux-m5-echo-v1\0')
class Peer(unittest.TestCase):
    def test_exact_identity_and_length_are_required(self):
        self.assertEqual(peer.validate_request(FRAME,7,2,False,1),1)
        for data,token,phase,udp,tx in [(FRAME,8,2,False,1),(FRAME,7,3,False,1),(FRAME,7,2,True,1),(FRAME,7,2,False,2),(b'CM3N'+FRAME[4:],7,2,False,1),(FRAME+b'x',7,2,False,1),(FRAME[:-1],7,2,False,1)]:
            with self.subTest(data=data,token=token,phase=phase,udp=udp,tx=tx),self.assertRaises(ValueError):peer.validate_request(data,token,phase,udp,tx)
    def test_loss_peer_does_not_wait_for_impossible_udp_request(self):
        output=io.StringIO();errors=[]
        def run():
            try:peer.serve(7,2,1,'loss',output,bind='127.0.0.1',ports=(0,0),software=True,duration=3)
            except Exception as error:errors.append(error)
        worker=threading.Thread(target=run,daemon=True);worker.start();deadline=time.monotonic()+2
        while not output.getvalue() and time.monotonic()<deadline:time.sleep(.01)
        ready=json.loads(output.getvalue().splitlines()[0])
        with socket.create_connection(('127.0.0.1',ready['tcp_port']),timeout=2) as tcp:
            tcp.sendall(FRAME)
        worker.join(.5)
        self.assertFalse(worker.is_alive(), 'Loss peer must not retain ports waiting for UDP while DOWN')
        self.assertEqual(errors, [])

    def test_real_tcp_udp_echo_and_peer_evidence(self):
        output=io.StringIO();errors=[]
        def run():
            try:peer.serve(7,2,1,'echo',output,bind='127.0.0.1',ports=(0,0),software=True,duration=3)
            except Exception as error:errors.append(error)
        worker=threading.Thread(target=run,daemon=True);worker.start();deadline=time.monotonic()+2
        while not output.getvalue() and time.monotonic()<deadline:time.sleep(.01)
        rows=[json.loads(s) for s in output.getvalue().splitlines()];self.assertTrue(rows)
        ready=rows[0]
        with socket.create_connection(('127.0.0.1',ready['tcp_port']),timeout=2) as tcp:
            for byte in FRAME:tcp.sendall(bytes([byte]))
            reply=b''
            while len(reply)<32:reply+=tcp.recv(32-len(reply))
            self.assertEqual(reply,FRAME)
        udp_frame=struct.pack('!4sIHHI16s',b'M5N1',7,2,2,1,b'crux-m5-echo-v1\0')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
            udp.settimeout(2);udp.sendto(udp_frame,('127.0.0.1',ready['udp_port']));self.assertEqual(udp.recv(33),udp_frame)
        worker.join(3);self.assertFalse(worker.is_alive());self.assertEqual(errors,[])
        rows=[json.loads(s) for s in output.getvalue().splitlines()]
        self.assertEqual([(r['protocol'],r['transaction'],r['request_hex'],r['response_hex']) for r in rows if r['event']=='exchange'],[('tcp',1,FRAME.hex(),FRAME.hex()),('udp',1,udp_frame.hex(),udp_frame.hex())])
if __name__=='__main__':unittest.main()

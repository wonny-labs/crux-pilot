"""Actual fixture consumer against controlled real Host TCP/UDP peers."""
from pathlib import Path
import errno
import socket
import subprocess
import tempfile
import threading
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Transaction(unittest.TestCase):
    def test_m5_exchange_framing_failures_and_separate_close_observation(self):
        code=r'''
#define _POSIX_C_SOURCE 200809L
#include "frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
int m5_test_close(int fd){struct timespec t={0,50000000};nanosleep(&t,0);return close(fd);}
int main(int argc,char **argv){if(argc!=3)return 2;uint8_t b[32];struct m5_metrics m={0};m5_frame(b,7,3,9,atoi(argv[2]));int r=m5_exchange("127.0.0.1",(uint16_t)atoi(argv[1]),atoi(argv[2]),b,100,&m);printf("%d %d %d %d %d %lld %lld %lld\n",r,m.functional_result,m.close_error,m.peak_owned,m.owned_after,(long long)m.entered_ms,(long long)m.functional_ms,(long long)m.returned_ms);}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);source=tmp/'main.c';source.write_text(code);obj=tmp/'transaction.o';binary=tmp/'test'
            flags=['cc','-std=c17','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/networking_physical')]
            subprocess.run(flags+['-Dclose=m5_test_close','-c',str(ROOT/'tests/networking_physical/transaction.c'),'-o',str(obj)],check=True)
            subprocess.run(flags+[str(source),str(obj),str(ROOT/'tests/networking_physical/frame.c'),'-o',str(binary)],check=True)
            for udp,mode,want in [(False,'echo',0),(True,'echo',0),(False,'close',-errno.ECONNRESET),(False,'silent',-errno.ETIMEDOUT),(True,'silent',-errno.ETIMEDOUT),(True,'wrong',-errno.EBADMSG)]:
                with self.subTest(udp=udp,mode=mode),socket.socket(socket.AF_INET,socket.SOCK_DGRAM if udp else socket.SOCK_STREAM) as server:
                    server.bind(('127.0.0.1',0));server.settimeout(2)
                    if not udp:server.listen(1)
                    received=[]
                    def peer():
                        if udp:
                            data,address=server.recvfrom(64);received.append(data)
                            if mode in ('echo','wrong'):server.sendto((b'x'+data[1:]) if mode=='wrong' else data,address)
                        else:
                            stream,_=server.accept()
                            with stream:
                                data=b''
                                while len(data)<32:
                                    part=stream.recv(32-len(data))
                                    if not part:break
                                    data+=part
                                received.append(data)
                                if mode=='echo':
                                    for byte in data:stream.sendall(bytes([byte]))
                                if mode=='silent':threading.Event().wait(.3)
                    worker=threading.Thread(target=peer,daemon=True);worker.start()
                    result=subprocess.run([str(binary),str(server.getsockname()[1]),str(int(udp))],text=True,capture_output=True,check=True,timeout=3)
                    worker.join(3)
                    values=list(map(int,result.stdout.split()))
                    self.assertEqual(values[:5],[want,want,0,1,0])
                    self.assertGreaterEqual(values[7]-values[6],40,'Close observation must remain distinct from functional deadline')
                    self.assertGreaterEqual(values[6],values[5])
                    expected=bytes.fromhex('4d354e31000000070003000'+('2' if udp else '1')+'00000009637275782d6d352d6563686f2d763100')
                    self.assertEqual(received,[expected])
if __name__=='__main__':unittest.main()

class ObservationFailure(unittest.TestCase):
    def test_failed_functional_timestamp_cannot_be_replaced_with_success(self):
        code=r'''
#define _POSIX_C_SOURCE 200809L
#include "frame.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static int calls, fail_at, at_close;
int m5_clock(clockid_t id,struct timespec *t){if(++calls==fail_at){errno=EIO;return -1;}return clock_gettime(id,t);}
int m5_close(int fd){at_close=calls;return close(fd);}
int main(int argc,char **argv){if(argc!=3)return 2;fail_at=atoi(argv[2]);uint8_t f[32];struct m5_metrics m;m5_frame(f,7,2,1,0);int r=m5_exchange("127.0.0.1",(uint16_t)atoi(argv[1]),0,f,1000,&m);printf("%d %d %lld %d %d\n",r,at_close,(long long)m.functional_ms,m.close_error,m.owned_after);}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);src=tmp/'main.c';src.write_text(code);obj=tmp/'transaction.o';exe=tmp/'test'
            flags=['cc','-std=c17','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/networking_physical')]
            subprocess.run(flags+['-Dclock_gettime=m5_clock','-Dclose=m5_close','-c',str(ROOT/'tests/networking_physical/transaction.c'),'-o',str(obj)],check=True)
            subprocess.run(flags+[str(src),str(obj),str(ROOT/'tests/networking_physical/frame.c'),'-o',str(exe)],check=True)
            count=0
            for failing in (False,True):
                with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as server:
                    server.bind(('127.0.0.1',0));server.listen(1);server.settimeout(2)
                    def peer():
                        stream,_=server.accept()
                        with stream:
                            data=b''
                            while len(data)<32:data+=stream.recv(32-len(data))
                            stream.sendall(data)
                    thread=threading.Thread(target=peer,daemon=True);thread.start()
                    values=list(map(int,subprocess.check_output([str(exe),str(server.getsockname()[1]),str(count if failing else 0)],text=True,timeout=3).split()))
                    thread.join(2)
                    if not failing:
                        self.assertEqual(values[0],0);count=values[1]
                    else:
                        self.assertEqual(values[0],-errno.EIO)
                        self.assertEqual(values[2:],[-1,0,0])

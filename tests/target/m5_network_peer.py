"""M5N1 application peer; fixed isolated hardware topology or explicit software loopback."""
import argparse
import json
import selectors
import signal
import socket
import struct
import m3_clock as clock

FRAME=struct.Struct('!4sIHHI16s')
PAYLOAD=b'crux-m5-echo-v1\0'

def validate_request(data,token,phase,udp,transaction):
    expected=FRAME.pack(b'M5N1',token,phase,2 if udp else 1,transaction,PAYLOAD)
    if data!=expected:
        raise ValueError('Wrong M5N1 length/version/nonce/phase/protocol/transaction/payload')
    return transaction

def emit(output,**record):
    output.write(json.dumps(dict(clock=clock.CLOCK_NAME,monotonic=clock.monotonic(),**record))+'\n');output.flush()

def receive(stream,deadline):
    data=b''
    while len(data)<32:
        remaining=deadline-clock.monotonic()
        if remaining<=0:raise TimeoutError('Peer frame deadline')
        stream.settimeout(remaining)
        chunk=stream.recv(32-len(data))
        if not chunk:raise EOFError('Incomplete M5N1 TCP frame')
        data+=chunk
    return data

def serve(token,phase,count,mode,output,*,bind='192.168.77.1',ports=(42420,42421),software=False,duration=160,stopping=lambda:False):
    if not (0<token<=0xffffffff and 1<=phase<=50 and 1<=count<=10 and mode in ('echo','silent','close','loss')):
        raise ValueError('Invalid peer phase')
    if (bind,ports)!=('192.168.77.1',(42420,42421)) and not (software and bind=='127.0.0.1'):
        raise ValueError('Peer must use isolated topology')
    deadline=clock.monotonic()+min(duration,160)
    expected={'tcp':1,'udp':count+1 if mode=='loss' else 1}
    with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as tcp,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp,selectors.DefaultSelector() as ready:
        tcp.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        tcp.bind((bind,ports[0]));udp.bind((bind,ports[1]));tcp.listen(1)
        ready.register(tcp,selectors.EVENT_READ);ready.register(udp,selectors.EVENT_READ)
        emit(output,event='ready',token=token,phase=phase,mode=mode,address=bind,tcp_port=tcp.getsockname()[1],udp_port=udp.getsockname()[1])
        while clock.monotonic()<deadline and not stopping() and any(n<=count for n in expected.values()):
            for key,_ in ready.select(max(0,min(.1,deadline-clock.monotonic()))):
                protocol='tcp' if key.fileobj is tcp else 'udp';transaction=expected[protocol]
                if transaction>count:raise ValueError('Duplicate/excess peer transaction')
                stream=None
                try:
                    if protocol=='tcp':
                        stream,address=tcp.accept();data=receive(stream,min(deadline,clock.monotonic()+3))
                    else:data,address=udp.recvfrom(4096)
                    if not software and address[0]!='192.168.77.2':raise ValueError('Unexpected peer source')
                    validate_request(data,token,phase,protocol=='udp',transaction)
                    emit(output,event='request',token=token,phase=phase,protocol=protocol,transaction=transaction,request_hex=data.hex())
                    response=b''
                    if mode=='echo' or (mode=='close' and protocol=='udp'):
                        response=data
                        if stream:
                            for offset in range(0,32,3):stream.sendall(response[offset:offset+3])
                        else:udp.sendto(response,address)
                    elif stream and mode in ('silent','loss'):
                        # No application response; promptly observe the target's close.
                        end=min(deadline,clock.monotonic()+10)
                        while not stopping() and clock.monotonic()<end:
                            stream.settimeout(min(.1,max(.001,end-clock.monotonic())))
                            try:
                                if stream.recv(1)==b'':break
                                raise ValueError('Excess TCP request bytes')
                            except socket.timeout:pass
                    emit(output,event='exchange',token=token,phase=phase,protocol=protocol,transaction=transaction,request_hex=data.hex(),response_hex=response.hex())
                    expected[protocol]+=1
                finally:
                    if stream:stream.close()
        if not stopping() and mode!='loss' and any(n<=count for n in expected.values()):raise TimeoutError('Missing peer transactions')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('token','phase','count'):p.add_argument('--'+name,type=int,required=True)
    p.add_argument('--mode',choices=('echo','silent','close','loss'),required=True)
    p.add_argument('--output',required=True)
    args=p.parse_args();state={'stop':False}
    signal.signal(signal.SIGPIPE,signal.SIG_IGN)
    for number in (signal.SIGTERM,signal.SIGINT):signal.signal(number,lambda *_:state.update(stop=True))
    with open(args.output,'x') as output:serve(args.token,args.phase,args.count,args.mode,output,stopping=lambda:state['stop'])

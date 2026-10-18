"""Bounded isolated M3 peer; never bind wildcard or management addresses."""
import argparse
import json
import selectors
import signal
import socket
import struct
import time

MAGIC = b'CM3N'
PAYLOAD = b'crux-m3-payload!'
FRAME = struct.Struct('!4sIII16s')
PEER = '192.168.77.1'
BOARD = '192.168.77.2'


def frame(protocol, trial, sequence):
    return FRAME.pack(MAGIC, protocol, trial, sequence, PAYLOAD)


def validate(data, protocol):
    if len(data) != FRAME.size:
        raise ValueError('Wrong frame length')
    magic, actual, trial, sequence, payload = FRAME.unpack(data)
    if magic != MAGIC or actual != protocol or payload != PAYLOAD or not trial or not sequence:
        raise ValueError('Invalid frame')
    return trial, sequence


def remaining(deadline):
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError('Exchange deadline expired')
    return value


def receive_exact(stream, size, deadline):
    data = bytearray()
    while len(data) < size:
        stream.settimeout(remaining(deadline))
        chunk = stream.recv(size - len(data))
        if not chunk:
            raise EOFError('Truncated TCP frame')
        data.extend(chunk)
    return bytes(data)


def send_exact(stream, data, deadline):
    view = memoryview(data)
    while view:
        stream.settimeout(remaining(deadline))
        count = stream.send(view)
        if count <= 0:
            raise EOFError('TCP send made no progress')
        view = view[count:]


def serve(duration, mode, output, stopping=lambda: False):
    deadline = time.monotonic() + duration
    count = 0
    with selectors.DefaultSelector() as ready, \
            socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp, \
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        # Permit bounded mode changes after our own accepted connections enter TIME_WAIT.
        # SO_REUSEPORT remains off; a live listener still owns this exact address.
        tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        tcp.bind((PEER, 42420))
        udp.bind((PEER, 42421))
        tcp.listen(1)
        tcp.setblocking(False)
        ready.register(tcp, selectors.EVENT_READ)
        ready.register(udp, selectors.EVENT_READ)
        output.write(json.dumps(dict(event='peer_ready', mode=mode, address=PEER,
                                     monotonic=time.monotonic())) + '\n')
        output.flush()
        while time.monotonic() < deadline and count < 200 and not stopping():
            for key, _ in ready.select(max(0, min(0.25, deadline - time.monotonic()))):
                if count >= 200 or time.monotonic() >= deadline or stopping():
                    break
                exchange_deadline = min(deadline, time.monotonic() + 2)
                protocol = 'tcp' if key.fileobj is tcp else 'udp'
                address = None
                data = b''
                try:
                    if key.fileobj is tcp:
                        with tcp.accept()[0] as stream:
                            address = stream.getpeername()
                            if address[0] != BOARD:
                                raise ValueError('Unexpected TCP peer')
                            data = receive_exact(stream, FRAME.size, exchange_deadline)
                            trial, sequence = validate(data, 1)
                            if mode == 'echo':
                                send_exact(stream, data, exchange_deadline)
                            elif mode == 'silence':
                                # Hold the accepted connection without sending a response.
                                while time.monotonic() < min(deadline, exchange_deadline + 0.25) and not stopping():
                                    time.sleep(0.01)
                            # close mode deliberately closes after receiving the frame.
                            protocol = 'tcp'
                    else:
                        udp.settimeout(remaining(exchange_deadline))
                        data, address = udp.recvfrom(FRAME.size + 1)
                        if address[0] != BOARD:
                            raise ValueError('Unexpected UDP peer')
                        trial, sequence = validate(data, 2)
                        if mode in ('echo', 'close'):
                            udp.settimeout(remaining(exchange_deadline))
                            if udp.sendto(data, address) != len(data):
                                raise OSError('Incomplete UDP send')
                        protocol = 'udp'
                    count += 1
                    record = dict(event='exchange', protocol=protocol, trial=trial,
                                  sequence=sequence, mode=mode, received_bytes=len(data),
                                  response=('sent' if mode == 'echo' or (protocol == 'udp' and mode == 'close')
                                            else 'closed' if mode == 'close' else 'withheld'),
                                  peer=address)
                except (OSError, ValueError, EOFError, TimeoutError) as error:
                    count += 1
                    record = dict(event='rejected', protocol=protocol, peer=address,
                                  received_bytes=len(data), response='not_sent', error=str(error))
                record['monotonic'] = time.monotonic()
                output.write(json.dumps(record) + '\n')
                output.flush()
    output.write(json.dumps(dict(event='peer_closed', exchanges=count,
                                 monotonic=time.monotonic())) + '\n')
    output.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=60, choices=range(1, 361))
    parser.add_argument('--mode', choices=('echo', 'silence', 'close'), default='echo')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    stop = [False]
    def request_stop(signum, frame):
        stop[0] = True
    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    with open(args.output, 'x') as output:
        serve(args.duration, args.mode, output, lambda: stop[0])


if __name__ == '__main__':
    main()

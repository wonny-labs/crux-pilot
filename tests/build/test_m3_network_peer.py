import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    'm3_network_peer', Path(__file__).resolve().parents[1] / 'target/m3_network_peer.py')
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)


class NetworkPeerControls(unittest.TestCase):
    def test_frame_is_fixed_and_rejects_bad_protocol_length_and_payload(self):
        data = peer.frame(1, 2, 3)
        self.assertEqual(len(data), 32)
        self.assertEqual(peer.validate(data, 1), (2, 3))
        for invalid in (data[:-1], data + b'x', b'x' + data[1:],
                        data[:-1] + b'x', peer.frame(1, 0, 3), peer.frame(1, 2, 0),
                        peer.frame(2, 2, 3)):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                peer.validate(invalid, 1)

    def test_partial_tcp_transfers_preserve_one_deadline(self):
        stream = Mock()
        stream.recv.side_effect = [b'ab', b'c', b'd']
        stream.send.side_effect = [1, 2, 1]
        with patch.object(peer.time, 'monotonic', return_value=10):
            self.assertEqual(peer.receive_exact(stream, 4, 12), b'abcd')
            peer.send_exact(stream, b'abcd', 12)
        self.assertEqual(stream.settimeout.call_count, 6)
        self.assertTrue(all(c.args == (2,) for c in stream.settimeout.call_args_list))

    def test_eof_no_progress_and_expired_deadline_are_failures(self):
        stream = Mock()
        stream.recv.return_value = b''
        stream.send.return_value = 0
        with patch.object(peer.time, 'monotonic', return_value=10):
            with self.assertRaises(EOFError):
                peer.receive_exact(stream, 32, 12)
            with self.assertRaises(EOFError):
                peer.send_exact(stream, b'abc', 12)
            with self.assertRaises(TimeoutError):
                peer.receive_exact(stream, 32, 10)

    def test_udp_modes_bounds_and_cleanup_use_only_declared_addresses(self):
        from io import StringIO
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        import json
        for mode in ('echo', 'silence', 'close'):
            with self.subTest(mode=mode):
                tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
                tcp.__enter__.return_value = tcp
                udp.__enter__.return_value = udp
                selector.__enter__.return_value = selector
                # A full batch must stop at200, including the final partial batch.
                selector.select.return_value = [(SimpleNamespace(fileobj=udp), 1)] * 3
                udp.recvfrom.return_value = (peer.frame(2, 1, 1), (peer.BOARD, 50000))
                udp.sendto.return_value = 32
                output = StringIO()
                with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
                     patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
                     patch.object(peer.time, 'monotonic', return_value=0):
                    peer.serve(1, mode, output)
                tcp.bind.assert_called_once_with((peer.PEER, 42420))
                tcp.setsockopt.assert_called_once_with(peer.socket.SOL_SOCKET, peer.socket.SO_REUSEADDR, 1)
                udp.bind.assert_called_once_with((peer.PEER, 42421))
                self.assertEqual(udp.recvfrom.call_count, 200)
                self.assertEqual(udp.sendto.call_count, 200 if mode in ('echo', 'close') else 0)
                self.assertEqual(udp.settimeout.call_count, 400 if mode in ('echo', 'close') else 200)
                self.assertEqual(json.loads(output.getvalue().splitlines()[-1])['event'], 'peer_closed')
                tcp.__exit__.assert_called_once()
                udp.__exit__.assert_called_once()
                selector.__exit__.assert_called_once()

    def test_invalid_udp_is_never_echoed_and_bind_failure_closes_resources(self):
        from io import StringIO
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        import json
        for data, source in ((peer.frame(2, 1, 1) + b'x', peer.BOARD),
                             (peer.frame(2, 1, 1), '192.168.77.9')):
            tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
            for item in (tcp, udp, selector):
                item.__enter__.return_value = item
            selector.select.return_value = [(SimpleNamespace(fileobj=udp), 1)]
            udp.recvfrom.return_value = (data, (source, 50000))
            output = StringIO()
            with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
                 patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
                 patch.object(peer.time, 'monotonic', return_value=0):
                peer.serve(1, 'echo', output)
            udp.sendto.assert_not_called()
            self.assertEqual(json.loads(output.getvalue().splitlines()[1])['event'], 'rejected')
        tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
        for item in (tcp, udp, selector):
            item.__enter__.return_value = item
        tcp.bind.side_effect = OSError('address unavailable')
        with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
             patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
             self.assertRaises(OSError):
            peer.serve(1, 'echo', StringIO())
        tcp.__exit__.assert_called_once()
        udp.__exit__.assert_called_once()

    def test_no_events_stops_at_global_deadline(self):
        from io import StringIO
        from unittest.mock import MagicMock
        tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
        for item in (tcp, udp, selector):
            item.__enter__.return_value = item
        clock = [0]
        def select(timeout):
            clock[0] += timeout
            return []
        selector.select.side_effect = select
        with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
             patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
             patch.object(peer.time, 'monotonic', side_effect=lambda: clock[0]):
            peer.serve(1, 'echo', StringIO())
        self.assertEqual(clock[0], 1)
        udp.recvfrom.assert_not_called()
        tcp.accept.assert_not_called()

    def test_tcp_modes_and_stream_cleanup(self):
        from io import StringIO
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        import json
        for mode in ('echo', 'silence', 'close'):
            with self.subTest(mode=mode):
                tcp, udp, selector, stream = (MagicMock() for _ in range(4))
                for item in (tcp, udp, selector, stream):
                    item.__enter__.return_value = item
                tcp.accept.return_value = (stream, (peer.BOARD, 50000))
                stream.getpeername.return_value = (peer.BOARD, 50000)
                stream.recv.return_value = peer.frame(1, 1, 1)
                stream.send.return_value = 32
                selector.select.return_value = [(SimpleNamespace(fileobj=tcp), 1)]
                clock = [0]
                def sleep(value):
                    clock[0] += value
                output = StringIO()
                with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
                     patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
                     patch.object(peer.time, 'monotonic', side_effect=lambda: clock[0]), \
                     patch.object(peer.time, 'sleep', side_effect=sleep):
                    peer.serve(1, mode, output)
                self.assertEqual(stream.send.call_count, 200 if mode == 'echo' else 0)
                self.assertEqual(stream.__exit__.call_count, 1 if mode == 'silence' else 200)
                record = json.loads(output.getvalue().splitlines()[1])
                self.assertEqual(record['protocol'], 'tcp')
                self.assertEqual(record['response'], 'sent' if mode == 'echo' else 'closed' if mode == 'close' else 'withheld')

    def test_spurious_accept_readiness_and_output_failure_are_bounded(self):
        from io import StringIO
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
        for item in (tcp, udp, selector):
            item.__enter__.return_value = item
        tcp.accept.side_effect = BlockingIOError('no connection')
        selector.select.return_value = [(SimpleNamespace(fileobj=tcp), 1)]
        with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
             patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
             patch.object(peer.time, 'monotonic', return_value=0):
            peer.serve(360, 'echo', StringIO())
        tcp.setblocking.assert_called_once_with(False)
        self.assertEqual(tcp.accept.call_count, 200)
        for failure in ('write', 'flush'):
            tcp, udp, selector, output = (MagicMock() for _ in range(4))
            for item in (tcp, udp, selector):
                item.__enter__.return_value = item
            getattr(output, failure).side_effect = OSError('evidence unavailable')
            with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
                 patch.object(peer.selectors, 'DefaultSelector', return_value=selector), \
                 self.assertRaises(OSError):
                peer.serve(1, 'echo', output)
            tcp.__exit__.assert_called_once()
            udp.__exit__.assert_called_once()
            selector.__exit__.assert_called_once()

    def test_requested_stop_closes_listener_without_exchange(self):
        from io import StringIO
        from unittest.mock import MagicMock
        import json
        tcp, udp, selector = MagicMock(), MagicMock(), MagicMock()
        for item in (tcp, udp, selector):
            item.__enter__.return_value = item
        output = StringIO()
        with patch.object(peer.socket, 'socket', side_effect=[tcp, udp]), \
             patch.object(peer.selectors, 'DefaultSelector', return_value=selector):
            peer.serve(300, 'echo', output, lambda: True)
        self.assertEqual(json.loads(output.getvalue().splitlines()[-1])['exchanges'], 0)
        tcp.accept.assert_not_called()
        tcp.__exit__.assert_called_once()
        udp.__exit__.assert_called_once()

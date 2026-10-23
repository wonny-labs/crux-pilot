import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class NetworkingBoundary(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('network_boundary', ROOT / 'scripts/check-networking-dependencies.py')
        self.checker = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.checker)

    def test_standard_profile_and_owned_contract_are_allowed(self):
        self.assertEqual(self.checker.source_errors('#include <sys/socket.h>\n#include <crux/netif.h>\nint f(void) { return socket(AF_INET, SOCK_STREAM, 0); }'), [])

    def test_native_header_cannot_be_used_through_macro(self):
        for code in ['#include <zephyr/net/socket.h>', '#include <fsl_enet.h>',
                     '#define HEADER <zephyr/kernel.h>\n#include HEADER',
                     '#include "../../components/netif/src/netif_port.h"']:
            with self.subTest(code=code): self.assertTrue(self.checker.source_errors(code))

    def test_native_symbols_and_conditionals_are_rejected(self):
        for code in ['zsock_socket(0,0,0);', 'net_if_up(iface);',
                     '#ifdef __ZEPHYR__\n#endif', '#if defined(__linux__)\n#endif',
                     'crux_netif_observe(&s);', 'k_sleep(1);', 'fsl_enet_init();',
                     'struct net_if *iface;', 'net_socklen_t length;', 'ENET_Type *enet;']:
            with self.subTest(code=code): self.assertTrue(self.checker.source_errors(code))

    def test_out_of_profile_operations_and_flags_are_rejected(self):
        for code in ['bind(fd, 0, 0);', 'listen(fd,1);', 'accept(fd,0,0);',
                     'sendto(fd,0,0,0,0,0);', 'recvfrom(fd,0,0,0,0,0);',
                     'send(fd,b,1,MSG_NOSIGNAL);']:
            with self.subTest(code=code): self.assertTrue(self.checker.source_errors(code))

    def test_private_link_or_include_cannot_be_consumer_requirement(self):
        self.assertEqual(self.checker.cmake_errors('target_link_libraries(consumer PRIVATE crux::sockets crux::netif)'), [])
        for code in ['target_link_libraries(consumer PRIVATE zephyr_interface)',
                     'target_link_libraries(consumer PRIVATE crux_netif)',
                     'target_include_directories(consumer PRIVATE ../../components/netif/src)']:
            with self.subTest(code=code): self.assertTrue(self.checker.cmake_errors(code))

    def test_physical_frame_allowance_is_local(self):
        code = '#include "frame.h"\n#include <poll.h>'
        self.assertTrue(self.checker.source_errors(code))
        self.assertEqual(self.checker.source_errors(code, owned={'frame.h'}), [])
        for forbidden in ('#include <zephyr/kernel.h>', 'net_if_up(0);', '#ifdef CONFIG_BOARD'):
            self.assertTrue(self.checker.source_errors(forbidden, owned={'frame.h'}))

    def test_comments_do_not_trigger_or_hide_direct_native_use(self):
        self.assertEqual(self.checker.source_errors('/* zsock_socket example */\n#include <poll.h>'), [])
        self.assertTrue(self.checker.source_errors('/* okay */ zsock_socket(0,0,0);'))

if __name__ == '__main__': unittest.main()

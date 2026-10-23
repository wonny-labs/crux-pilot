"""Exact ELF/BIN geometry and writable-storage exclusion, using synthetic nonflashable inputs."""
from pathlib import Path
import struct,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
from test_m3_image import fixture
import m5_image
class Image(unittest.TestCase):
    def image(self,root):
        paths=fixture(root,'network');data=bytearray(paths[1].read_bytes());offset=data.index(b'unused_stack\0');data[offset:offset+12]=b'm5_observer\0';struct.pack_into('<I',data,0x2318,2112);struct.pack_into('<I',data,0x2330,0);paths[1].write_bytes(data)
        cfg=paths[2].read_text().replace('CONFIG_SHELL=y','# CONFIG_SHELL is not set').replace('CONFIG_WATCHDOG=y','# CONFIG_WATCHDOG is not set').replace('CONFIG_CRUX_M3_NETWORK_PROBE=y','# CONFIG_CRUX_M3_NETWORK_PROBE is not set')+'\nCONFIG_CRUX_SOCKETS=y\nCONFIG_CRUX_NETIF=y\nCONFIG_CONSOLE_GETCHAR=y\nCONFIG_CONSOLE_SUBSYS=y\nCONFIG_INIT_STACKS=y\nCONFIG_THREAD_STACK_INFO=y\nCONFIG_PRINTK_SYNC=y\n# CONFIG_LOG is not set\n# CONFIG_FLASH is not set\n'
        paths[2].write_text(cfg)
        return paths
    def test_exact_new_image_and_stack_ownership(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=self.image(Path(tmp));r=m5_image.check_image(*p)
            self.assertEqual((r['bin_address'],r['main_stack']['bytes'],r['observer_stack']['bytes']),(0x60000000,4160,2112))
    def test_tampering_and_writable_storage_are_rejected(self):
        for mutate in ['binary','storage','stack']:
            with self.subTest(mutate=mutate),tempfile.TemporaryDirectory() as tmp:
                p=self.image(Path(tmp))
                if mutate=='binary':p[0].write_bytes(p[0].read_bytes()[:-1]+b'x')
                if mutate=='storage':p[2].write_text(p[2].read_text()+'CONFIG_FLASH=y\n')
                if mutate=='stack':
                    data=bytearray(p[1].read_bytes());struct.pack_into('<I',data,0x2318,4160);p[1].write_bytes(data)
                with self.assertRaises(ValueError):m5_image.check_image(*p)
if __name__=='__main__':unittest.main()

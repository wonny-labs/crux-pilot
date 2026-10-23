"""Compile real fixture wire encoder; literal identity catches stale M3/epoch frames."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Frame(unittest.TestCase):
    def test_wire_identity_is_exact_and_network_ordered(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'main.c';binary=Path(tmp)/'test'
            source.write_text('#include "frame.h"\n#include <stdio.h>\nint main(void){uint8_t b[32];m5_frame(b,0x12345678,3,9,true);for(int i=0;i<32;++i)printf("%02x",b[i]);}\n')
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/networking_physical'),str(source),str(ROOT/'tests/networking_physical/frame.c'),'-o',str(binary)],check=True)
            result=subprocess.check_output([str(binary)],text=True)
            self.assertEqual(result,'4d354e31123456780003000200000009637275782d6d352d6563686f2d763100')
if __name__=='__main__':unittest.main()

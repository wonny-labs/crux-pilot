"""M5 uses its actual consumer compile entry; M3's default remains strict."""
import hashlib,json,tempfile,unittest
from pathlib import Path
from test_m3_errno import fixture
from m3_errno import verify_errno_map
class M5Errno(unittest.TestCase):
    def test_private_m5_entry_is_explicit_and_m3_default_cannot_admit_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            p,image,record=fixture(Path(tmp));entry=record['compile_command'];entry['file']='/source/transaction.c';entry['command']=entry['command'].replace('network_client.c','transaction.c')
            record['compile_command_sha256']=hashlib.sha256(json.dumps(entry,sort_keys=True).encode()).hexdigest()
            p.write_text(json.dumps(record));Path(image['artifacts'][0]['path']).write_text(json.dumps([entry]))
            with self.assertRaises(ValueError):verify_errno_map(p,image)
            try:values=verify_errno_map(p,image,source_suffix='/transaction.c')
            except TypeError:self.fail('M5 actual compile entry cannot yet be selected explicitly')
            self.assertEqual(values['ETIMEDOUT'],116)
if __name__=='__main__':unittest.main()

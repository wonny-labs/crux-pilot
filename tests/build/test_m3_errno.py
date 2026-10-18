import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
from m3_errno import verify_errno_map, NAMES
from prepare_m3_errno import preprocess_command


def digest(value):
    return hashlib.sha256(value).hexdigest()


def fixture(folder):
    binary=folder/'image.bin';binary.write_bytes(b'identified-image')
    command=dict(file='/source/network_client.c',directory='/build',command='/sdk/gcc -DSOURCE=1 -o result.o -c /source/network_client.c')
    compilation=folder/'compile_commands.json';compilation.write_text(json.dumps([command]))
    values=dict(zip(NAMES,(5,115,114,104,128,116,111,118)))
    macros=''.join('#define '+name+' '+str(value)+'\n' for name,value in values.items())
    (folder/'macros.txt').write_text(macros)
    (folder/'header-trace.txt').write_text('. /sdk/include/sys/errno.h\n')
    (folder/'header-errno.h').write_text(macros)
    (folder/'compiler-executable.bin').write_bytes(b'compiler')
    record=dict(schema=1,source_commit='source',image_bin_sha256=digest(binary.read_bytes()),values=values,
                compile_command=command,compile_command_sha256=digest(json.dumps(command,sort_keys=True).encode()),
                preprocessing_command=preprocess_command(command),
                compiler=dict(path='/sdk/gcc',artifact='compiler-executable.bin',sha256=digest(b'compiler')),headers=[dict(original_path='/sdk/include/sys/errno.h',
                copy='header-errno.h',sha256=digest(macros.encode()))],artifacts=[dict(path=name,
                sha256=digest((folder/name).read_bytes())) for name in ('macros.txt','header-trace.txt','header-errno.h','compiler-executable.bin')])
    path=folder/'errno-map.json';path.write_text(json.dumps(record))
    image=dict(binary=str(binary),source=dict(git_commit='source'),artifacts=[dict(path=str(compilation))])
    return path,image,record


class TargetErrnoIdentity(unittest.TestCase):
    def test_selected_compiler_map_binds_exact_image(self):
        with tempfile.TemporaryDirectory() as directory:
            path,image,_=fixture(Path(directory))
            self.assertEqual(verify_errno_map(path,image)['ETIMEDOUT'],116)

    def test_changed_image_source_macros_headers_and_command_rejected(self):
        for change in ('image','source','value','header','command','missing','preprocess','compiler-path','compiler-sha','compiler-bytes'):
            with self.subTest(change=change),tempfile.TemporaryDirectory() as directory:
                folder=Path(directory);path,image,record=fixture(folder)
                if change=='image':Path(image['binary']).write_bytes(b'other-image')
                elif change=='source':record['source_commit']='other'
                elif change=='value':record['values']['ETIMEDOUT']=110
                elif change=='header':(folder/'header-errno.h').write_text('other')
                elif change=='command':record['compile_command']['command']+=" -DOTHER=1"
                elif change=='missing':del record['values']['EHOSTUNREACH']
                elif change=='preprocess':record['preprocessing_command'].append('-DOTHER=1')
                elif change=='compiler-path':record['compiler']['path']='/sdk/other'
                elif change=='compiler-sha':record['compiler']['sha256']='b'*64
                elif change=='compiler-bytes':(folder/'compiler-executable.bin').write_bytes(b'other')
                path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):verify_errno_map(path,image)

    def test_preprocessing_preserves_flags_and_never_writes_build_outputs(self):
        entry=dict(file='/source/network_client.c',command='/sdk/gcc -DFLAG=1 -MD -MF object.d -MT object -o object.o -c /source/network_client.c')
        self.assertEqual(preprocess_command(entry),['/sdk/gcc','-DFLAG=1','-E','-dM','-H','-x','c','-'])


if __name__=='__main__':unittest.main()

"""Provider selection and independent portable ARM consumer usage checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT/'samples/flash_target_candidate'
class Selection(unittest.TestCase):
    def configure(self, candidate, native=False, app=None, board='mimxrt1060_evk@B/mimxrt1062/qspi'):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            folder.joinpath('CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(consumer C CXX)
set(CONFIG_BOARD_NATIVE_SIM {'ON' if native else 'OFF'})
set(CONFIG_FLASH_SIMULATOR {'ON' if native else 'OFF'})
set(CONFIG_BOARD_TARGET "{board}")
set(CONFIG_FILE_SYSTEM ON)
set(CONFIG_FILE_SYSTEM_LITTLEFS ON)
set(CRUX_RT1060_STORAGE_SAMPLE {'ON' if candidate else 'OFF'})
set(APPLICATION_SOURCE_DIR "{app or SAMPLE}")
add_library(zephyr_interface INTERFACE)
function(zephyr_append_cmake_library target)
endfunction()
add_subdirectory("{ROOT}/components/file/ports/zephyr" file)
get_target_property(flags crux_file_api INTERFACE_COMPILE_OPTIONS)
file(WRITE "${{CMAKE_BINARY_DIR}}/usage.txt" "${{flags}}")
''')
            result=subprocess.run(['cmake','-S',str(folder),'-B',str(folder/'build')],capture_output=True,text=True)
            flags=(folder/'build/usage.txt').read_text() if result.returncode==0 else ''
            return result,flags
    def test_exact_sample_connects_without_host_abi_flags(self):
        result,flags=self.configure(True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertNotIn('-m32',flags);self.assertNotIn('-m64',flags)
        self.assertNotIn('-fno-pie',flags);self.assertNotIn('-fno-pic',flags)
    def test_other_application_board_and_default_refuse(self):
        for args in [dict(candidate=False),dict(candidate=True,app='/tmp/other'),
                     dict(candidate=True,board='mimxrt1060_evk@A/mimxrt1062/qspi')]:
            result,_=self.configure(**args)
            self.assertNotEqual(result.returncode,0)
    def test_native_width_selection_preserved(self):
        result,flags=self.configure(False,native=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('-m32',flags);self.assertIn('-fno-pie',flags)
    def test_unsupported_native_profiles_refuse_before_provider_selection(self):
        profiles=[{'CONFIG_NVS_DATA_CRC':'OFF'}, {'CONFIG_SETTINGS_NVS_NAME_CACHE':'ON'},
                  {'CONFIG_NVS_INIT_BAD_MEMORY_REGION':'ON'}, {'CONFIG_USERSPACE':'ON'},
                  {'CONFIG_FILE_SYSTEM_LITTLEFS':'OFF'}, {'CONFIG_SETTINGS_NVS':'OFF'}]
        for change in profiles:
            values={'CONFIG_BOARD_TARGET':'mimxrt1060_evk@B/mimxrt1062/qspi',
                    'CONFIG_FILE_SYSTEM_LITTLEFS':'ON','CONFIG_SETTINGS_NVS':'ON',
                    'CONFIG_NVS_DATA_CRC':'ON'}
            values.update(change)
            with tempfile.TemporaryDirectory() as tmp:
                script=Path(tmp)/'profile.cmake'
                script.write_text('\n'.join('set('+key+' "'+value+'")' for key,value in values.items())+
                    '\ninclude("'+str(SAMPLE/'storage.cmake')+'")\n')
                result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('Unsupported RT1060 private storage profile',result.stderr)
if __name__=='__main__':unittest.main()

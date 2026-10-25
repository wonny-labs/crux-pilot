"""Execute actual RAM terminal stores in a disposable host child, no target access."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / 'samples/rom_observation_diagnostic'

class Terminal(unittest.TestCase):
    def test_actual_terminal_publishes_containment_and_never_returns(self):
        code = '''
#define _GNU_SOURCE
#include "observation.h"
#include <assert.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile struct crux_observation_record *shared_record;
#define crux_observation_result (*shared_record)
''' + (SAMPLE / 'terminal.c').read_text() + '''
int main(void) {
    shared_record=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(shared_record != MAP_FAILED);
    pid_t child=fork(); assert(child >= 0);
    if (!child) crux_flash_poc_terminal(-ETIMEDOUT);
    struct timespec pause={.tv_nsec=1000000};
    unsigned retries=1000;
    while(shared_record->phase != CRUX_OBSERVATION_CONTAINED && retries--) nanosleep(&pause,NULL);
    int status=0;
    assert(waitpid(child,&status,WNOHANG)==0); /* child did not return */
    assert(kill(child,SIGKILL)==0); assert(waitpid(child,&status,0)==child);
    assert(retries > 0 && shared_record->phase==CRUX_OBSERVATION_CONTAINED);
    assert(shared_record->result==-ETIMEDOUT);
    assert(shared_record->disposition==CRUX_OBSERVATION_RAM_CONTAINED);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'case.c').write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(SAMPLE), '-I' + str(ROOT / 'samples/flash_target_candidate'),
                            str(tmp / 'case.c'), '-o', str(tmp / 'case')], check=True)
            subprocess.run([str(tmp / 'case')], check=True, timeout=5)

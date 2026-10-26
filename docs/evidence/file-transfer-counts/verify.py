import json
from pathlib import Path
import subprocess

out = Path('/workspace/crux-results')
results = []

def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    results.append(dict(cwd=str(cwd), command=cmd, returncode=p.returncode, output=p.stdout))
    (out/'verification.json').write_text(json.dumps(results, indent=2)+'\n')
    if p.returncode:
        raise RuntimeError(f'{cmd}: {p.stdout}')

for name, root in [('main', Path('/workspace/crux-pilot')), ('candidate56', Path('/workspace/crux-candidate56'))]:
    for mode in ['debug', 'sanitize']:
        build = out/name/mode
        build.mkdir(parents=True, exist_ok=True)
        flags = ['-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-UNDEBUG', '-g']
        if mode == 'sanitize':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        includes = ['-Icomponents/file/include', '-Icomponents/file/src', '-Icomponents/file/ports/linux']
        sources = ['components/file/src/file.c', 'components/file/ports/linux/file_linux.c']
        if name == 'candidate56':
            sources[1] = 'components/file/ports/linux/file_host.c'
            sources += ['components/file/ports/linux/file_lock.c']
        for target, test in [('fault', 'tests/file/fault_test.c'), ('host', 'tests/file/host_test.c')]:
            selected = ['components/file/src/file.c'] if target == 'fault' else sources
            exe = str(build/target)
            run(['/usr/bin/gcc', *flags, '-D_POSIX_C_SOURCE=200809L', *includes, test, *selected, '-o', exe], root)
            run([exe], root)
        for source in sources:
            obj = str(build/(Path(source).stem+'.o'))
            run(['/usr/bin/gcc', *flags, '-D_POSIX_C_SOURCE=200809L', *includes, '-c', source, '-o', obj], root)
        fixture = str(build/'fixture.o')
        run(['/usr/bin/gcc', *flags, '-D_POSIX_C_SOURCE=200809L', *includes, '-c', 'tests/file_consumer/fixture.c', '-o', fixture], root)
        for suffix, compiler, standard in [('c', '/usr/bin/gcc', '-std=c17'), ('cpp', '/usr/bin/g++', '-std=c++17')]:
            obj, exe = str(build/('consumer-'+suffix+'.o')), str(build/('consumer-'+suffix))
            consumer_flags = [f for f in flags if f != '-std=c17']
            # Consumer compilation gets only the public include directory.
            run([compiler, standard, *consumer_flags, '-Icomponents/file/include', '-c', 'tests/file_consumer/consumer.'+suffix, '-o', obj], root)
            run([compiler, *consumer_flags, fixture, obj, *[str(build/(Path(s).stem+'.o')) for s in sources], '-o', exe], root)
            run([exe], root)
print(f'{len(results)} compile/link/run commands passed; 16 executable runs')

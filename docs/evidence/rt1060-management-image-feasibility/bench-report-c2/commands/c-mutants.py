# Mutants of the C formatter and policy (report/bench_report_format.c, bench_report_policy.c): each weakens one rule, and
# report/bench_report_host_test.c must fail. Run from the repository root; prints one line per mutant.
import shutil
import subprocess
import tempfile

V = 'tests/zephyr/management_rt1060_compile/report/'
MUTANTS = [
    ('bench_report_format.c', 'if (w->length + 1u >= w->capacity) {', 'if (w->length + 1u > w->capacity) {',
     'capacity off by one (writes past the buffer)'),
    ('bench_report_format.c', "if (name == NULL || name[0] == '\\0' || unused > size) {",
     "if (name == NULL || name[0] == '\\0') {", 'a row with unused above size is accepted'),
    ('bench_report_format.c', 'if (w->overflow) {\n        return refuse(w);\n    }',
     "if (w->overflow) {\n        w->buffer[w->length] = '\\0';\n        return w->length;\n    }", 'a cut line is returned'),
    ('bench_report_format.c', "(c >= 'A' && c <= 'F');", "(c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');",
     'lower case fingerprint accepted'),
    ('bench_report_format.c', "name_valid(name[length]) ? name[length] : '_'", 'name[length]',
     'name bytes not sanitised'),
    ('bench_report_policy.c', 'if (since_print < BENCH_REPORT_MIN_GAP_MS) {', 'if (false) {', 'no minimum gap'),
    ('bench_report_policy.c', 'still >= BENCH_REPORT_QUIET_MS', 'still > BENCH_REPORT_QUIET_MS + 100000u',
     'quiet never reached'),
    ('bench_report_policy.c', 'policy->idle_done = false;\n        if (!policy->active)', 'if (!policy->active)',
     'idle is not re-armed by a burst'),
    ('bench_report_policy.c', 'uint32_t still = now_ms - policy->last_change_ms;',
     'int64_t still = (int64_t)now_ms - (int64_t)policy->last_change_ms;', 'wrap of the clock not handled'),
]
work = tempfile.mkdtemp()
originals = {name: open(V + name).read() for name in {m[0] for m in MUTANTS}}
results = []
try:
    for name, old, new, description in MUTANTS:
        assert old in originals[name], (name, old)
        open(V + name, 'w').write(originals[name].replace(old, new, 1))
        build = subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-fsanitize=address,undefined', '-I' + V,
                                V + 'bench_report_host_test.c', V + 'bench_report_format.c',
                                V + 'bench_report_policy.c', '-o', work + '/mutant'], capture_output=True, text=True)
        if build.returncode != 0:
            results.append(('compile error', description))
        else:
            run = subprocess.run([work + '/mutant'], capture_output=True, text=True)
            results.append(('killed' if run.returncode != 0 else 'SURVIVED', description))
finally:
    for name, text in originals.items():
        open(V + name, 'w').write(text)
    shutil.rmtree(work)
for outcome, description in results:
    print(f'{outcome:10} {description}')
print(sum(1 for outcome, _ in results if outcome != 'SURVIVED'), 'of', len(results), 'killed')

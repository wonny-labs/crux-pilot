import subprocess, shutil
P='tests/zephyr/management_rt1060_compile/check_bench_uart.py'
orig=open(P).read()
S='/private/tmp/claude-502/-Users-jiwonkim-Projects/62404e4d-6996-484d-bf84-9ae6f358d48f/scratchpad/'
M=[('if http["unused_min"] < needed:','if http["unused_min"] <= needed:','margin compared with <= (off by one)'),
('entry["unused_min"] = min(entry["unused_min"], row["unused"])','pass','lowest unused not kept over reports'),
('if used + unused != size:','if False:','row that does not add up accepted'),
('if lines != counted:','if False:','lost line not noticed'),
('if expected_seq is not None and seq != expected_seq','if False and seq != expected_seq','gap in seq accepted'),
('certificate != reference','False','different certificate accepted'),
('capture.certificates[-1]','capture.certificates[0]','first boot compared instead of the last'),
('if peak * 100 > arena * options.heap_peak_limit_percent:','if False:','heap limit ignored'),
('if missed != 0:','if False:','missed threads accepted'),
('http["size"] != expected_http','False','declared stack size not compared'),
('if clients < options.min_client_reports:','if False:','missing client report accepted'),
('if reports and not any(report["reason"] == "boot" for report in reports):','if False:','missing boot report accepted'),
('if len(line) > LINE_LIMIT:','if False:','over-long line accepted')]
res=[]
try:
    for a,b,d in M:
        assert a in orig,a
        open(P,'w').write(orig.replace(a,b,1))
        r=subprocess.run(['python3','-m','unittest','tests/build/test_management_bench_report.py'],capture_output=True,text=True)
        res.append(('killed' if r.returncode!=0 else 'SURVIVED',d))
finally:
    open(P,'w').write(orig)
for o,d in res: print(f'{o:9} {d}')
print(sum(o=='killed' for o,_ in res),'of',len(res),'killed')

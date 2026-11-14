# usage: post.sh NAME   static stack chains of the report thread, from the ELF
set -e
name=$1
E=/build/$name/zephyr/zephyr.elf
printf 'report_loop\nbench_report_core_snapshot\nbench_report_format_stack\nbench_report_format_cert\ncrux_bench_report_certificate\ncrux_bench_report_main_exit\ncollect\nrecord_worker_stack\nnote_ended_threads\n' > /build/roots-c2.txt
python3 /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/commands/callgraph.py $E /build/roots-c2.txt /build/$name.chains.json
cat /build/$name.chains.json | head -80

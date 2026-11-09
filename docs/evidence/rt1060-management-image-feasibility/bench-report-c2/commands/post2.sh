set -e
printf 'printk\nvprintk\nz_log_printk\nz_log_msg_runtime_vcreate\nk_thread_foreach_unlocked\nk_thread_stack_space_get\nmalloc_runtime_stats_get\nbench_report_core_snapshot\nreport_loop\ncollect\nk_mutex_lock\nk_msleep\n' > /build/roots-c2b.txt
python3 /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/commands/callgraph.py /build/bench_c2.zephyr.elf /build/roots-c2b.txt /build/chains2.json
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
$SDK/arm-zephyr-eabi-nm -S --size-sort /build/bench_c2.zephyr.elf | grep -iE "bench_report|report_loop|record_worker|note_ended|tcp_segments|crux_ssh_wolf_worker_stack_ended| report_stack|ended_rows| rows$|report_lock|report_thread|line$|sequence|bursts_seen" > /build/c2-symbols.txt
cat /build/c2-symbols.txt

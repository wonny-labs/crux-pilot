export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
bash /scripts/fmt_c4.sh
bash /scripts/c4_host.sh

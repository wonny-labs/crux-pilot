export PATH=/opt/crux-cache/venv/bin:$PATH
cd /ws/crux-pilot
python3 scripts/check_docs.py 2>&1 | tail -15
echo "rc=${PIPESTATUS[0]}"

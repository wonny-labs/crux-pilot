"""Use production printk format literals rather than mirrored output strings."""
import argparse
import ast
import json
from pathlib import Path
import re
p=argparse.ArgumentParser();p.add_argument('sample',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
formats={}
for name in ('network_client.c','network_probe.c'):
    source=(a.sample/name).read_text()
    for match in re.finditer(r'printk\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)(?:,|\))',source):
        value=''.join(ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"',match[1]))
        if not value.startswith('M3_NET_'):continue
        key=value.split(' ')[0].strip().removeprefix('M3_NET_')
        if key=='CLEANUP':key+='_'+('OK' if 'result=0 ' in value else 'FENCE')
        if key in formats and formats[key]!=value:raise ValueError('Ambiguous production format '+key)
        formats[key]=value
required={'REQUESTED','ROUND_BUDGET','CLEANUP_OK','EXCHANGE','RESOURCE','STATE','ROUND','STACK','LOGGER'}
if not required.issubset(formats):raise ValueError('Missing production formats')
a.output.write_text('/* Generated from the actual reference network sources. */\n'+''.join('#define M3_FMT_'+k+' '+json.dumps(formats[k])+'\n' for k in sorted(required)))

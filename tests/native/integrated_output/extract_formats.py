"""Extract the actual integrated variants of production printk literals."""
import argparse,ast,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('sample',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
variants={}
for name in ('m3_main.c','network_client.c'):
    for match in re.finditer(r'printk\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)(?:,|\))',(a.sample/name).read_text()):
        value=''.join(ast.literal_eval(t) for t in re.findall(r'"(?:\\.|[^"\\])*"',match[1]))
        if value.startswith('M3_'):variants.setdefault(value.split(' ')[0][3:],set()).add(value)
selected={}
filters={'BOOT':'profile=integrated','OBSERVER_ALIVE':'network_sequence=', 'STACK':'network_status=',
         'WDT_FEED':'network_sequence=', 'WDT_ARMED':'network_freshness_ms=',
         'FAULT_STALL':'requested_ms=', 'NET_CLEANUP':'result=0 '}
required=('BOOT','WORK','OBSERVER_ALIVE','STACK','LOGGER','WDT_FEED','WDT_ARMED',
          'FAULT_STALL','NET_FAULT_STALL','NET_WORK','NET_RESOURCE','NET_STATE','NET_EXCHANGE','NET_CLEANUP')
for key in required:
    values=[s for s in variants.get(key,()) if filters.get(key,'') in s]
    if len(values)!=1:raise ValueError('Missing/ambiguous integrated format '+key)
    selected[key]=values[0]
a.output.write_text('/* Generated production formats; fixture values are synthetic. */\n'+''.join(
    '#define M3_FMT_'+key+' '+json.dumps(value)+'\n' for key,value in sorted(selected.items())))

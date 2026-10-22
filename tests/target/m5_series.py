"""Strict three-boot M5 physical evidence replay; imports no device libraries.

Host receipt/peer/control times share CLOCK_MONOTONIC. Target clocks are compared
only within a boot. A cable claim requires independent host and target witnesses;
no subtraction of clocks with unrelated origins is permitted.
"""
import math
import re
import m5_network_oracle as oracle

PHASES=((1,0,'idle'),(2,10,'echo'),(3,1,'silent'),(4,1,'echo'),(5,1,'close'),(6,1,'echo'),(7,1,'loss'),(8,1,'echo'),(9,0,'idle'))
CLOCK='clock_gettime(CLOCK_MONOTONIC)'

def times(rows,key='monotonic'):
    values=[r[key] for r in rows]
    oracle.require(all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in values),'Invalid host clock')
    oracle.require(all(a<=b for a,b in zip(values,values[1:])),'Misordered host evidence')
    return values

def one(rows,event):
    selected=[r for r in rows if r['event']==event]
    oracle.require(len(selected)==1,'Missing/duplicate '+event)
    return selected[0]

def loss_correlation(controls,events,peers):
    allowed={'cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route'}
    oracle.require(len(controls)==6 and {r['event'] for r in controls}==allowed,'Missing/duplicate/unapproved physical operation witness')
    times(controls)
    cue=one(controls,'cable_disconnect_cue');receipt=one(controls,'cable_disconnect_receipt');down=one(controls,'host_down')
    connect=one(controls,'cable_connect_receipt');up=one(controls,'host_up');route=one(controls,'route')
    oracle.require(down.get('interface')==up.get('interface')==route.get('interface')=='en5','Wrong dedicated NIC identity')
    oracle.require((route.get('source'),route.get('destination'))==('192.168.77.1','192.168.77.2'),'Wrong isolated route')
    active=[r for r in events if r['record'].get('seq')==7 and r['record']['event'] in ('BEGIN','END') and r['record'].get('udp')==0]
    oracle.require([r['record']['event'] for r in active]==['BEGIN','END'],'Missing active loss stage')
    request=one([r for r in peers['7'] if r.get('protocol')=='tcp'],'request')
    oracle.require(request['monotonic']<=cue['monotonic'] and active[0]['monotonic']<=cue['monotonic']<=active[1]['monotonic'] and cue['monotonic']<=receipt['monotonic'] and cue['monotonic']<=down['monotonic'],'Disconnect not corroborated during active TCP call')
    one([r['record'] for r in events if r['record'].get('seq')==7],'DONE')
    ready8=next((r for r in events if r['record']['event']=='READY' and r['record'].get('seq')==8),None)
    oracle.require(ready8 is not None and ready8['monotonic']<connect['monotonic'],'Reconnect before complete loss/settling/READY8')
    udp_end=next(r['record'] for r in events if r['record']['event']=='END' and r['record'].get('seq')==7 and r['record'].get('udp')==1)
    down_target=[r['record'] for r in events if r['record']['event']=='LINK' and r['record']['at']<=udp_end['functional']]
    oracle.require(down_target and max(down_target,key=lambda r:r['at'])['link']==1,'UDP attempt not confirmed while DOWN')
    oracle.require(not any(r['record']['event']=='LINK' and r['record']['link']==2 and active[0]['record']['at']<=r['record']['at']<=udp_end['functional'] for r in events),'Target recovered before loss UDP completion')
    recovery=next((r for r in events if r['record'].get('seq')==8 and r['record']['event']=='BEGIN'),None)
    oracle.require(recovery is not None,'Missing resetless recovery')
    links=[r for r in events if r['record']['event']=='LINK' and r['record']['link']==2 and r['monotonic']>active[1]['monotonic']]
    oracle.require(len(links)==1,'Missing/duplicate resetless target UP')
    link=links[0];record=link['record']
    oracle.require(record['ipv4']==3232255234 and record['admin']==1 and record['address']==1,'Recovered primary interface not configured')
    oracle.require(max(active[1]['monotonic'],receipt['monotonic'],down['monotonic'])<connect['monotonic']<=up['monotonic']<=route['monotonic']<=recovery['monotonic'] and connect['monotonic']<=link['monotonic']<=recovery['monotonic'] and max(up['monotonic'],route['monotonic'],link['monotonic'])-connect['monotonic']<=15,'Recovery not independently confirmed before communication')
    oracle.require(record['at']<=recovery['record']['at'],'Target UP follows recovery BEGIN')

def validate_primary(row):
    oracle.require(row['link'] in (0,1,2) and row['admin']==1 and row['address']==1 and row['ipv4']==3232255234,'Configured primary interface identity/status changed')
    oracle.require(row['link']!=0 or row.get('seq',0)<=1,'Unknown link after initial observation phase')


def validate_series(series,source,abi):
    oracle.require(re.fullmatch('[0-9a-f]{12}',source) is not None,'Expected source must be exact 12-digit revision')
    oracle.require(series.get('schema')==1 and series.get('source')==source and series.get('clock')==CLOCK,'Wrong series provenance/clock')
    boots=series.get('boots',[])
    oracle.require(len(boots)==3 and len({b['nonce'] for b in boots})==3,'Require three independently identified fresh boots')
    maximum=0;previous_finish=-1
    for index,boot in enumerate(boots,1):
        token=boot['nonce'];all_events=boot['uart'];reset=boot['reset']
        times(all_events)
        events=[e for e in all_events if e['record']['event']!='LINK']
        oracle.require(type(token) is int and 0<token<=0xffffffff,'Invalid boot nonce')
        stamps=times(events)
        oracle.require(reset['operation']=='qualification_reset_'+str(index) and previous_finish<reset['monotonic']<stamps[0],'Missing/misordered independently approved fresh reset')
        oracle.require(stamps[-1]-reset['monotonic']<=1800,'Series liveness limit')
        observer=[e for e in all_events if e['record']['event']=='LINK']
        for e in observer:validate_primary(e['record'])
        oracle.require(len({(e['record']['at'],e['record']['link']) for e in observer})==len(observer) and all(a['record']['at']<=b['record']['at'] for a,b in zip(observer,observer[1:])),'Duplicate/misordered observer stream')
        rows=[e['record'] for e in events]
        oracle.require(len(rows)>=3 and rows[0]==dict(event='BOOT',source=source) and rows[1].get('event')=='ARM' and rows[2].get('event')=='READY','Boot/ARM/READY prefix incomplete')
        # Re-parse all target records through the strict public record schema.
        for row in [e['record'] for e in all_events if e['record']['event']!='BOOT']:
            text='M5 '+row['event']+' '+' '.join(str(k)+'='+str(v) for k,v in row.items() if k!='event')
            oracle.require(oracle.parse_record(text)==row and row['token']==token,'Malformed/foreign target evidence')
        oracle.require(rows[2]['seq']==1 and rows[2]['ticket']>0,'Invalid first readiness ticket')
        writes=boot['writes'];times(writes)
        oracle.require(len(writes)==10 and writes[0]['line']=='A '+str(token)+'\n' and events[0]['monotonic']<=writes[0]['monotonic']<=events[1]['monotonic'],'Missing/excess ARM writes')
        cursor=3;tickets={rows[2]['ticket']};last_main=rows[2]['at']
        oracle.require(set(boot['peers'])=={str(s) for s,c,_ in PHASES if c},'Missing/extra peer phase')
        for sequence,count,mode in PHASES:
            start=cursor
            while cursor<len(rows) and rows[cursor]['event']!='READY':cursor+=1
            oracle.require(cursor<len(rows),'Missing phase readiness/completion')
            selected=rows[start:cursor];ready=rows[cursor]
            oracle.require(selected and all(r.get('seq')==sequence for r in selected),'Missing/misordered phase')
            main=[r for r in selected if r['event']!='LINK']
            oracle.require(main and main[0]['at']>=last_main,'Target main clock reversed across commands')
            oracle.require(ready['seq']==sequence+1 and ready['ticket']>0 and ready['ticket'] not in tickets and ready['at']>=main[-1]['at'],'Replayed/misordered readiness ticket')
            tickets.add(ready['ticket']);last_main=ready['at']
            action='I' if not count else 'L' if sequence==7 else 'P'
            expected=' '.join([action,str(token),str(sequence)]+([str(count)] if action=='P' else [])+[str(rows[start-1]['ticket'])])+'\n'
            write=writes[sequence]
            oracle.require(write['line']==expected and events[start-1]['monotonic']<=write['monotonic']<=next(e['monotonic'] for e in events[start:cursor] if e['record']['event']!='LINK'),'Missing/replayed/queued/misordered command write')
            command_start=write['monotonic'];finish=events[cursor]['monotonic']
            oracle.require(finish-command_start<150,'Absolute command liveness limit')
            if sequence==2:
                initial=[e for e in observer if e['monotonic']<=command_start]
                oracle.require(initial and initial[-1]['record']['link']==2,'Configured primary UP not observed before initial traffic')
            if count:
                links=[e['record'] for e in all_events if e['record']['event']=='LINK' and e['record'].get('seq')==sequence and command_start<=e['monotonic']<=finish]
                result=oracle.validate_command(selected+links,token,sequence,count,mode,abi)
                maximum=max(maximum,result['max_call_ms'])
                peer=boot['peers'][str(sequence)];oracle.validate_peer(peer,token,sequence,count,mode)
                oracle.require(peer[0]['monotonic']<=command_start,'Peer not ready before target traffic')
                watch=oracle.Watch(command_start,command_start+150,reset['monotonic']+1800)
                for event in events[start:cursor]:watch.feed(event['record']['event'],event['monotonic'])
                watch.check(finish)
                # Every peer request lies within its corresponding UART receipt interval.
                for request in [p for p in peer if p['event']=='request']:
                    matches=[e for e in events[start:cursor] if e['record']['event'] in ('BEGIN','END') and e['record']['round']==request['transaction']-1 and e['record']['udp']==(request['protocol']=='udp')]
                    oracle.require(len(matches)==2 and command_start<=request['monotonic']<=matches[1]['monotonic'],'Peer request outside correlated active call')
            else:
                oracle.require([r['event'] for r in main]==['IDLE','IDLE','IDLE','DONE'],'Incomplete idle baseline')
                oracle.validate_idle(selected,token,sequence,0,main[0]['at'])
                oracle.require(main[-1]['at']>=main[-2]['at'],'Idle DONE before samples')
            cursor+=1
        oracle.require(cursor==len(rows),'Unconsumed/replayed target evidence')
        stops=boot['peer_stops'];times(stops)
        oracle.require(len(stops)==7 and {p['phase'] for p in stops}==set(range(2,9)) and all(p.get('reaped') is True for p in stops),'Missing/duplicate/unreaped peer process')
        for stop in stops:
            phase=stop['phase'];ready=next(e for e in events if e['record']['event']=='READY' and e['record']['seq']==phase+1)
            oracle.require(ready['monotonic']<=stop['monotonic'],'Peer stopped before complete target cleanup')
            if phase<8:oracle.require(stop['monotonic']<=boot['peers'][str(phase+1)][0]['monotonic'],'Next peer started before prior process reaped')
            if phase==7:oracle.require(stop['monotonic']<=one(boot['controls'],'cable_connect_receipt')['monotonic'],'Reconnect before loss peer reaped')
        loss_correlation(boot['controls'],all_events,boot['peers'])
        previous_finish=stamps[-1]
    oracle.require(previous_finish-boots[0]['reset']['monotonic']<=1800,'Whole series exceeds 1800 seconds')
    return dict(status='REVIEW_REQUIRED' if maximum>2500 else 'PASS',boots=3,pairs=48,calls=96,max_call_ms=maximum,loss_scope='TCP in-flight disconnect; UDP attempt while DOWN',recovery='same boot, no reset')

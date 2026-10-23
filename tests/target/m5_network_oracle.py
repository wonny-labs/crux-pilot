"""M5-only physical evidence rules. No target access or M3 wire-format reuse."""
import re
import math
import struct

class Inconclusive(ValueError):
    pass

def require(condition, message):
    if not condition:
        raise ValueError(message)

FIELDS={
    'ARM':{'token','at'},
    'READY':{'token','seq','ticket','at'},
    'BEGIN':{'token','seq','round','udp','at'},
    'END':{'token','seq','round','udp','at','result','entered','functional','close_error','peak','owned'},
    'LINK':{'token','seq','at','link','admin','address','ipv4'},
    'IDLE':{'token','seq','checkpoint','at','contexts','rx_pkt','tx_pkt','rx_buf','tx_buf','main_free','observer_free'},
    'DONE':{'token','seq','at'},
}

def parse_record(line):
    parts=line.split(' ')
    require(len(parts)>=3 and parts[0]=='M5' and parts[1] in FIELDS,'Malformed/unknown M5 record')
    result={'event':parts[1]}
    for field in parts[2:]:
        match=re.fullmatch(r'([a-z_][a-z_0-9]*)=(-?[0-9]+)',field)
        require(match and match[1] not in result,'Malformed/duplicate M5 field')
        result[match[1]]=int(match[2])
    require(set(result)-{'event'}==FIELDS[parts[1]],'Incomplete/extra M5 fields')
    require(result['at']>=0 and result['token']>0,'Invalid epoch/time')
    return result

def validate_idle(rows, token, sequence, checkpoint, returned):
    samples=[r for r in rows if r['event']=='IDLE' and r['checkpoint']==checkpoint]
    require(len(samples)==3,'Missing/duplicate resource samples')
    for row in samples:
        require((row['contexts'],row['rx_pkt'],row['tx_pkt'],row['rx_buf'],row['tx_buf'])==(0,16,16,32,32),'Resources not returned')
        require(1024<=row['main_free']<=4096 and 512<=row['observer_free']<=2048,'Stack headroom/accounting failed')
    times=[r['at'] for r in samples]
    require(returned<=times[0] and times[-1]-returned<=5000 and all(b-a>=1000 for a,b in zip(times,times[1:])),'Resource settling/idle sampling deadline failed')
    return times[-1]

def validate_command(rows, token, sequence, count, mode, abi):
    require(mode in ('echo','silent','close','loss') and 1<=count<=10,'Unsupported case')
    require(all(r.get('token')==token and r.get('seq')==sequence for r in rows),'Foreign epoch/phase')
    require(all(r.get('event') in ('BEGIN','END','IDLE','LINK','DONE') for r in rows),'Unexpected event')
    links=[r for r in rows if r['event']=='LINK']
    require(len({(r['at'],r['link']) for r in links})==len(links),'Duplicate link observation')
    require(all(a['at']<=b['at'] for a,b in zip(links,links[1:])),'Misordered observer records')
    main=[r for r in rows if r['event']!='LINK']
    require(all(a['at']<=b['at'] for a,b in zip(main,main[1:])),'Misordered main records')
    require(sum(r['event']=='DONE' for r in main)==1 and main[-1]['event']=='DONE','Missing/duplicate/early DONE')
    require(len([r for r in main if r['event'] in ('BEGIN','END')])==count*4,'Missing/duplicate transactions')
    require(len([r for r in main if r['event']=='IDLE'])==count*6,'Missing/duplicate checkpoints')
    previous=-1;maximum=0;loss_seen=False
    for round_number in range(count):
        for udp in (0,1):
            selected=[r for r in main if r['event'] in ('BEGIN','END') and r.get('round')==round_number and r.get('udp')==udp]
            require([r['event'] for r in selected]==['BEGIN','END'],'Orphan/duplicate/reordered transfer')
            begin,end=selected
            require(previous<=begin['at']<=end['entered']<=end['functional']<=end['at'],'Invalid transfer clock/order')
            require(end['close_error']==0 and end['owned']==0 and end['peak'] in (0,1),'Uncertain socket cleanup')
            maximum=max(maximum,end['at']-end['entered'])
            want=0 if mode=='echo' or (mode=='close' and udp) else -abi['ECONNRESET'] if mode=='close' else -abi['ETIMEDOUT']
            allowed={want} if mode!='loss' else {-abi[n] for n in ('ETIMEDOUT','ENETDOWN','ENETUNREACH','ECONNRESET')}
            require(end['result'] in allowed,'Wrong symbolic result')
            if mode=='loss' and udp==0:
                downs=[r for r in rows if r['event']=='LINK' and r['link']==1 and end['entered']<=r['at']<=end['functional']]
                loss_seen=bool(downs)
            previous=validate_idle(rows,token,sequence,round_number*2+udp,end['at'])
    require(previous<=main[-1]['at'],'Completion precedes cleanup')
    if mode=='loss' and not loss_seen:
        raise Inconclusive('No target DOWN observation inside active functional communication')
    return {'status':'REVIEW_REQUIRED' if maximum>2500 else 'PASS','max_call_ms':maximum}

class Watch:
    def __init__(self, started, command_deadline, series_deadline):
        self.absolute=min(command_deadline,series_deadline)
        self.progress=started+10
        self.active=False
    def check(self, now):
        if now>=min(self.absolute,self.progress):
            raise Inconclusive('External liveness expired; target may still execute; no cancellation/reset')
    def feed(self,event,now):
        self.check(now)
        if event=='BEGIN':
            require(not self.active,'Nested BEGIN')
            self.active=True;self.progress=now+10
        elif event=='END':
            require(self.active,'END without BEGIN')
            self.active=False;self.progress=now+10

def validate_peer(rows,token,phase,count,mode):
    require(rows and rows[0]['event']=='ready','Missing peer readiness')
    ready=rows[0]
    require((ready['address'],ready['tcp_port'],ready['udp_port'],ready['mode'])==('192.168.77.1',42420,42421,mode),'Wrong isolated peer/mode')
    require(all(r.get('clock')=='clock_gettime(CLOCK_MONOTONIC)' and type(r.get('monotonic')) in (int,float) and math.isfinite(r['monotonic']) and r['monotonic']>=0 and r.get('token')==token and r.get('phase')==phase for r in rows),'Wrong peer epoch/clock')
    require(all(a['monotonic']<=b['monotonic'] for a,b in zip(rows,rows[1:])),'Misordered peer records')
    require(all(r['event'] in ('ready','request','exchange') for r in rows) and sum(r['event']=='ready' for r in rows)==1,'Unexpected/duplicate peer event')
    protocols=('tcp',) if mode=='loss' else ('tcp','udp')
    require(len(rows)==1+2*count*len(protocols),'Missing/duplicate peer evidence')
    for protocol in protocols:
        selected=[r for r in rows[1:] if r.get('protocol')==protocol]
        require(len(selected)==count*2,'Missing peer protocol')
        for tx in range(1,count+1):
            pair=[r for r in selected if r.get('transaction')==tx]
            require([r['event'] for r in pair]==['request','exchange'],'Orphan/duplicate/reordered peer transaction')
            expected=struct.pack('!4sIHHI16s',b'M5N1',token,phase,1 if protocol=='tcp' else 2,tx,b'crux-m5-echo-v1\0').hex()
            require(all(r['request_hex']==expected for r in pair),'Peer wire identity differs')
            response=expected if mode=='echo' or (mode=='close' and protocol=='udp') else ''
            require(pair[1]['response_hex']==response,'Peer did not perform declared response behavior')
    return True

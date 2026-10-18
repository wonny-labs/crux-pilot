"""Replayable combined-controller physical verdicts; raw rows and peer frames required."""
import re
from m3_health_oracle import require
from m3_network_oracle import records


def stacks(rows):
    samples=records(rows,'M3_STACK')
    require(samples,'Missing combined stack observations')
    observers=[i for i,(_,line) in enumerate(rows) if 'M3_OBSERVER_ALIVE ' in line]
    require(len(samples)==len(observers),'Missing/extra observer stack observation')
    for n,index in enumerate(observers):
        upper=observers[n+1] if n+1<len(observers) else len(rows)
        following=records(rows[index+1:upper],'M3_STACK')
        require(len(following)==1 and 0<=following[0]['received']-rows[index][0]<=.25,
                'Observer stack pairing/order/bound failed')
    for r in samples:
        require(all(r[p+'_status']==0 and r[p+'_usable']==4096 and 1024<=r[p+'_unused']<=4096
                    for p in ('worker','main','network')), 'Combined stack query/margin failure')
        require(r['worker_object']==r['network_object']==4160,'Combined guard/object mismatch')
    return {p+'_min_unused':min(r[p+'_unused'] for r in samples) for p in ('worker','main','network')}


def pairs(rows,peer,partial_after=None):
    require(peer and peer[0].get('clock')=='clock_gettime(CLOCK_MONOTONIC)', 'Peer/receive clock domain mismatch')
    work=records(rows,'M3_NET_WORK')
    exchanges=records(rows,'M3_NET_EXCHANGE')
    frames=[r for r in peer if r['event']=='exchange']
    require(exchanges and peer[0]['event']=='peer_ready' and peer[-1]['event']=='peer_closed' and
            peer[0]['monotonic']<=exchanges[0]['received']<=exchanges[-1]['received']<=peer[-1]['monotonic'],
            'Peer not live for all actual exchanges')
    rejected=[r for r in peer if r['event']=='rejected']
    require(not rejected or (partial_after is not None and len(rejected)<=1 and all(
        r['monotonic']>=partial_after and r['peer'][0]=='192.168.77.2' and
        r['received_bytes']<32 and r['error'] in ('Truncated TCP frame','timed out')
        for r in rejected)), 'Peer rejected frame')
    require(len([r for r in peer if r['event']=='peer_ready'])==1 and
            len([r for r in peer if r['event']=='peer_closed'])==1,'Incomplete peer lifecycle')
    require(peer[-1]['event']=='peer_closed' and peer[-1]['exchanges']==len(frames)+len(rejected),'Peer count/lifecycle mismatch')
    expected=[(p,s) for s in range(1,len(work)+1) for p in (1,2)]
    completed_exchanges=[r for r in exchanges if r['sequence']<=len(work)]
    completed_frames=[r for r in frames if r['sequence']<=len(work)]
    require([(r['protocol'],r['sequence']) for r in completed_exchanges]==expected,'Incomplete/unordered full pairs')
    require([(1 if r['protocol']=='tcp' else 2,r['sequence']) for r in completed_frames]==expected,'Both-end pairs disagree')
    for extra,clock,key in (([r for r in exchanges if r['sequence']>len(work)],'received','protocol'),
                            ([r for r in frames if r['sequence']>len(work)],'monotonic','protocol')):
        require(not extra or (partial_after is not None and len(extra)<=2 and all(
            r['sequence']==len(work)+1 and r[clock]>=partial_after-14 for r in extra)),
            'Unbounded or premature reset-cancelled prefix')
        protocols=[r[key] if isinstance(r[key],int) else 1 if r[key]=='tcp' else 2 for r in extra]
        require(protocols==[1,2][:len(extra)],'Cancelled pair prefix has duplicate/out-of-order protocols')
    for r in exchanges:
        require(r['trial']==1 and r['result']==0 and 0<=r['elapsed_ms']<=2250,'Pair payload/budget failure')
    for r in frames:
        require(r['protocol'] in ('tcp','udp') and r['trial']==1 and r['mode']=='echo' and r['received_bytes']==32 and
                r['peer'][0]=='192.168.77.2' and r['response']=='sent','Wrong peer frame identity')
    cleanup=records(rows,'M3_NET_CLEANUP')
    require(cleanup and all(r['result']==0 and r['restart_required']==0 and
                           r['expected_contexts']==r['observed_contexts']==0 and
                           0<=r['elapsed_ms']<=5250 for r in cleanup),'Native cleanup/fence failure')
    for r in records(rows,'M3_NET_RESOURCE'):
        require(r['owned']<=1 and r['contexts']<=1 and 0<=r['rx_pkt_free']<=16 and
                0<=r['tx_pkt_free']<=16 and 0<=r['rx_buf_free']<=32 and
                0<=r['tx_buf_free']<=32,'Unbounded combined native resource use')
    require([r['sequence'] for r in work]==list(range(1,len(work)+1)), 'Network progress identity')
    require(all(b['completed_ms']-a['completed_ms']>=1000 for a,b in zip(work,work[1:])), 'Catch-up/overlap progress')
    previous_completion=None
    for w in work:
        steps=[r for r in completed_exchanges if r['sequence']==w['sequence']]
        require(steps[0]['monotonic_ms']<=steps[1]['monotonic_ms']<=w['completed_ms'] and
                w['completed_ms']-(steps[0]['monotonic_ms']-steps[0]['elapsed_ms'])<=14250,
                'Pair completion outside its target budget')
        if previous_completion is not None:
            require(steps[0]['monotonic_ms']-steps[0]['elapsed_ms']-previous_completion>=1000, 'Pair start overlaps completion gap')
        previous_completion=w['completed_ms']
        indices=[i for i,(_,line) in enumerate(rows) if 'M3_NET_EXCHANGE ' in line and
                 any(r['sequence']==w['sequence'] for r in records([(0,line)],'M3_NET_EXCHANGE'))]
        completed_index=next(i for i,(_,line) in enumerate(rows) if 'M3_NET_WORK ' in line and
                             records([(0,line)],'M3_NET_WORK')[0]['sequence']==w['sequence'])
        require(len(indices)==2 and indices[0]<indices[1]<completed_index,'Pair source event ordering failed')
        for left,right in ((indices[0],indices[1]),(indices[1],completed_index)):
            returns=records(rows[left+1:right],'M3_NET_CLEANUP')
            require(len(returns)==1 and returns[0]['result']==0 and
                    returns[0]['expected_contexts']==returns[0]['observed_contexts']==0 and
                    returns[0]['restart_required']==0,'Completed pair lacks both native cleanup returns')
    return work


def default_state(rows,source,commands):
    require(rows and rows[0][1].startswith('*** Booting Zephyr') and
            sum(line.startswith('*** Booting Zephyr') for _,line in rows)==1, 'Default state has wrong native boot count')
    require(not re.search(r'FATAL ERROR|ASSERTION FAIL|M3_\w*(?:FAILED|REJECTED|FAULT_STALL|STALL_REQUESTED)', '\n'.join(line for _,line in rows)),
            'Default state runtime diagnostic/fault')
    require(not records(rows,'M3_NET_WORK'),'Automatic network progress')
    boots=[line for _,line in rows if line.startswith('M3_BOOT ')]
    require(boots==[f'M3_BOOT source={source} profile=integrated commands={commands} fault_default=off'],
            'Wrong combined source/profile/default state')
    require(not records(rows,'M3_NET_WINDOW') and not records(rows,'M3_NET_EXCHANGE') and
            not records(rows,'M3_WDT_FEED') and not records(rows,'M3_WDT_ARMED'), 'Automatic window/arm/traffic')
    observers=records(rows,'M3_OBSERVER_ALIVE')
    require(len(observers)>=4 and observers[-1]['monotonic_ms']-observers[0]['monotonic_ms']>=3000 and
            all(r['healthy']==r['readiness']==r['network_active']==0 for r in observers),
            'Missing required-network unready baseline')
    work=records(rows,'M3_WORK')
    require(len(work)>=4 and work[-1]['sequence']>work[0]['sequence'] and
            all(r['device']==0 and r['time'] in (0,-4) for r in work),'Time/GPIO component not advancing')
    return dict(status='PASS',**stacks(rows))


def validate_watchdog(rows,peer,source,owner):
    require(owner in ('time','network'),'Wrong fault owner')
    require(rows and all(a[0]<=b[0] for a,b in zip(rows,rows[1:])) and rows[-1][0]-rows[0][0]<=90,
            'Capture clock/bound violated')
    text='\n'.join(s for _,s in rows)
    require(not re.search(r'M3_\w*(?:FAILED|REJECTED)|FATAL ERROR|ASSERTION FAIL',text),'Runtime diagnostic/fault rejection')
    natives=[i for i,(_,s) in enumerate(rows) if s.startswith('*** Booting Zephyr')]
    boots=[i for i,(_,s) in enumerate(rows) if s.startswith('M3_BOOT ')]
    require(len(natives)==len(boots)==2 and natives[0]==0 and natives[0]<boots[0]<natives[1]<boots[1],
            'Expected exactly two fresh native/source boots')
    for native,boot in zip(natives,boots):
        require(rows[boot][1]==f'M3_BOOT source={source} profile=integrated commands=1 fault_default=off' and
                0<=rows[boot][0]-rows[native][0]<=.5,'Wrong/buffered source boot')
    before,after=rows[:natives[1]],rows[natives[1]:]
    windows=records(before,'M3_NET_WINDOW')
    require(len(windows)==1 and 59999<=windows[0]['end_ms']-windows[0]['started_ms']<=60000,'Wrong one-shot window')
    window=windows[0]
    require(not records(before,'M3_NET_WINDOW_STOP'),'Window expiry/network failure cannot supply a watchdog pass')
    arm=records(before,'M3_WDT_ARMED')
    require(len(arm)==1 and arm[0]['timeout_ms']==2000 and arm[0]['debug_pause']==1 and
            arm[0]['freshness_ms']==500 and arm[0]['network_freshness_ms']==16000,'Wrong watchdog arm/config')
    name='M3_FAULT_STALL' if owner=='time' else 'M3_NET_FAULT_STALL'
    faults=records(before,name)
    require(len(faults)==1 and not records(before,'M3_NET_FAULT_STALL' if owner=='time' else 'M3_FAULT_STALL'),
            'Wrong/missing/duplicate owner fault')
    fault=faults[0]
    fault_index=next(i for i,(_,line) in enumerate(before) if name+' ' in line)
    requests=records(before,'M3_STALL_REQUESTED' if owner=='time' else 'M3_NET_STALL_REQUESTED')
    require(len(requests)==1 and requests[0]['requested_ms']==fault['requested_ms'] and
            requests[0]['deadline_ms']==fault['deadline_ms'],'Fault does not match exact target request')
    require(fault['deadline_ms']-fault['requested_ms']==15000 and
            fault['requested_ms']<=fault['monotonic_ms']<fault['deadline_ms'] and
            fault['window_end_ms']==window['end_ms'] and window['end_ms']-fault['monotonic_ms']>=25000,
            'Owner injection expired/outside declared window budget')
    require(fault['received']-arm[0]['received']>=12,'Healthy observation shorter than six watchdog intervals')
    work=pairs(before,peer,fault['received'] if owner=='time' else None)
    healthy_pairs=[r for r in work if arm[0]['received']<=r['received']<fault['received']]
    require(len(healthy_pairs)>=2,'Missing two new full pairs during healthy interval')
    feeds=records(before,'M3_WDT_FEED')
    require(feeds and feeds[0]['received']>=arm[0]['received'] and
            fault['monotonic_ms']-feeds[0]['monotonic_ms']>=12000,'Insufficient target healthy feeding')
    healthy_feeds=[r for r in feeds if r['monotonic_ms']<fault['monotonic_ms']]
    require(healthy_feeds[-1]['sequence']>healthy_feeds[0]['sequence'] and
            healthy_feeds[-1]['network_sequence']>healthy_feeds[0]['network_sequence'],'Required owners did not advance')
    work_by_sequence={r['sequence']:r for r in work}
    for r in feeds:
        require(r['network_sequence'] in work_by_sequence and
                work_by_sequence[r['network_sequence']]['completed_ms']==r['network_completed_ms'] and
                work_by_sequence[r['network_sequence']]['received']<=r['received'],
                'Feed is not bound to an actual completed TCP/UDP pair')
        require(0<=r['monotonic_ms']-r['completed_ms']<=500 and
                0<=r['monotonic_ms']-r['network_completed_ms']<=16000 and
                r['monotonic_ms']<window['end_ms'],'Stale/future/out-of-window feed')
    for a,b in zip(feeds,feeds[1:]):
        require(0<b['monotonic_ms']-a['monotonic_ms']<=250 and
                b['sequence']>=a['sequence'] and b['completed_ms']>=a['completed_ms'] and
                b['network_sequence']>=a['network_sequence'] and
                b['network_completed_ms']>=a['network_completed_ms'] and
                0<=b['received']-a['received']<=.5 and
                abs(b['received']-a['received']-(b['monotonic_ms']-a['monotonic_ms'])/1000)<=.15,
                'Feed clock/progress discontinuity')
    limit=750 if owner=='time' else 16250
    require(0<=feeds[-1]['monotonic_ms']-fault['monotonic_ms']<=limit,'Late/missing feed cutoff after owner stall')
    if owner=='network':
        require(feeds[-1]['monotonic_ms']-fault['completed_ms']<=16250 and
                not records(before[fault_index+1:],'M3_NET_WORK') and not records(before[fault_index+1:],'M3_NET_EXCHANGE'),'Network owner advanced after fault')
    else:
        require(not records(before[fault_index+1:],'M3_WORK'),'Time owner advanced after fault')
    stale=records(before,'M3_HEALTH_STALE')
    require(len(stale)==1 and fault['monotonic_ms']<=stale[0]['monotonic_ms']<window['end_ms'], 'Missing timely stale observer')
    reset_delay=rows[natives[1]][0]-feeds[-1]['received']
    require(1<=reset_delay<=5 and
            feeds[-1]['monotonic_ms']+reset_delay*1000<window['end_ms'],'Reset timing/expiry relationship failed')
    if owner=='network':
        continued=[r for r in records(before,'M3_WORK') if r['received']>fault['received']]
        require(len(continued)>=2 and continued[-1]['sequence']>continued[0]['sequence'],'Time/GPIO did not continue during network stall')
    observers=records(before,'M3_OBSERVER_ALIVE')
    pre=[]
    for r in observers:
        if r['received']>=arm[0]['received']:break
        if r['healthy']==r['readiness']==1:pre.append(r)
        else:pre=[]
    require(len(pre)>=2 and 0<=arm[0]['received']-pre[-1]['received']<=1.25 and
            all(750<=b['monotonic_ms']-a['monotonic_ms']<=1250 for a,b in zip(pre,pre[1:])) and
            pre[-1]['monotonic_ms']-pre[0]['monotonic_ms']>=1000 and
            window['end_ms']-feeds[0]['monotonic_ms']>=40000,'Arm admission not evidenced')
    require(any(r['received']>=stale[0]['received'] and r['healthy']==r['readiness']==0 for r in observers),
            'Independent observer not alive/unready after stall')
    for part in (before,after):
        reset=records(part,'M3_RESET')
        require(len(reset)==1 and reset[0]['support_status']==reset[0]['cause_status']==
                reset[0]['clear_status']==reset[0]['remaining_status']==0 and
                int(reset[0]['supported'],16)&0x10 and int(reset[0]['remaining'],16)==0,'Reset cause support/read/clear failed')
    require(int(records(after,'M3_RESET')[0]['cause'],16)&0x10,'Watchdog reset cause absent')
    default_state(after,source,1)
    return dict(status='PASS',owner=owner,last_feed_to_boot_receive_s=reset_delay,
                full_pairs=len(work),healthy_new_pairs=len(healthy_pairs),**stacks(rows))


def idle_resources(rows):
    resource=records(rows,'M3_NET_RESOURCE')
    require(len(resource)>=3,'Missing combined idle resource checkpoint')
    first=resource[:3]
    indices=[i for i,(_,line) in enumerate(rows) if 'M3_NET_RESOURCE ' in line]
    for n,index in enumerate(indices[:3]):
        upper=indices[n+1] if n+1<len(indices) else len(rows)
        state=records(rows[index+1:upper],'M3_NET_STATE')
        require(len(state)==1 and 0<=state[0]['received']-rows[index][0]<=.25 and
                0<=state[0]['monotonic_ms']-first[n]['monotonic_ms']<=250 and
                state[0]['received']-rows[0][0]<=5 and state[0]['address']==1,
                'Idle resource/state pairing failed')
    require(0<=first[-1]['received']-rows[0][0]<=5,'Idle checkpoint exceeded original 5 s window')
    require(first[-1]['monotonic_ms']-first[0]['monotonic_ms']>=1800 and
            all(750<=b['monotonic_ms']-a['monotonic_ms']<=1250 for a,b in zip(first,first[1:])),
            'Idle reports not consecutive 1 s observations')
    require(all([r[k] for k in ('owned','contexts','rx_pkt_free','tx_pkt_free','rx_buf_free','tx_buf_free')]
                ==[0,0,16,16,32,32] for r in first),'Combined native idle baseline did not return')
    return dict(samples=3,span_ms=first[-1]['monotonic_ms']-first[0]['monotonic_ms'])


def validate_functional(rows,peer,source,gpio_interval=None):
    text='\n'.join(s for _,s in rows)
    require(rows and 0<=rows[-1][0]-rows[0][0]<=80,'Functional capture exceeded80s')
    require(rows and rows[0][1].startswith('*** Booting Zephyr') and text.count('*** Booting Zephyr')==1 and
            text.count(f'M3_BOOT source={source} profile=integrated commands=1 fault_default=off')==1,
            'Wrong/fresh combined functional boot')
    require(not re.search(r'M3_\w*(?:FAILED|REJECTED)|FATAL ERROR|ASSERTION FAIL|M3_WDT_|M3_\w*FAULT',text),
            'Unexpected functional fault/watchdog/diagnostic')
    windows=records(rows,'M3_NET_WINDOW');stops=records(rows,'M3_NET_WINDOW_STOP')
    require(len(windows)==len(stops)==1 and 59999<=windows[0]['end_ms']-windows[0]['started_ms']<=60000 and
            stops[0]['result']==0 and windows[0]['end_ms']<=stops[0]['monotonic_ms']<=windows[0]['end_ms']+250,
            'Window expiry/status/budget mismatch')
    start=next(i for i,(_,s) in enumerate(rows) if 'M3_NET_WINDOW ' in s)
    stop=next(i for i,(_,s) in enumerate(rows) if 'M3_NET_WINDOW_STOP ' in s)
    default_state(rows[:start],source,1)
    idle_resources(rows[:start]);idle_resources(rows[stop+1:])
    require(not records(rows[stop+1:],'M3_NET_EXCHANGE'),'New transaction after window expiry')
    work=pairs(rows,peer)
    require(len(work)>=2,'Missing actual combined TCP/UDP pairs')
    observers=records(rows,'M3_OBSERVER_ALIVE')
    runs=[];ready=[]
    for r in observers:
        if r['readiness']==r['healthy']==1:
            if ready and not 750<=r['monotonic_ms']-ready[-1]['monotonic_ms']<=1250:
                runs.append(ready);ready=[]
            ready.append(r)
        else:
            if ready:runs.append(ready)
            ready=[]
    if ready:runs.append(ready)
    require(any(len(run)>=6 and run[-1]['monotonic_ms']-run[0]['monotonic_ms']>=5000 for run in runs),
            'No sustained joint readiness')
    post=records(rows[stop+1:],'M3_OBSERVER_ALIVE')
    require(post and all(r['readiness']==r['healthy']==r['network_active']==0 for r in post),
            'Window expiry did not revoke required readiness')
    device=records(rows,'M3_WORK')
    require(device and all(r['device']==0 and r['time'] in (0,-4) and r['pressed'] in (0,1) for r in device),
            'Actual Time/GPIO work failed')
    groups=[]
    if gpio_interval:
        begin,end=gpio_interval
        values=[r for r in device if begin<=r['received']<=end]
        require(values and values[0]['pressed']==values[-1]['pressed']==0,'Missing released stimulus baseline/handoff')
        for r in values:
            if not groups or groups[-1][0]!=r['pressed']:groups.append((r['pressed'],[r]))
            else:groups[-1][1].append(r)
        require(len(groups)==11 and [g[0] for g in groups]==[0,1]*5+[0], 'Missing/extra press-release groups')
        for value,group in groups[1:-1]:
            require(group[-1]['monotonic_ms']-group[0]['monotonic_ms']>=2000,'Short observed press/release group')
        require(groups[-1][1][-1]['monotonic_ms']-groups[-1][1][0]['monotonic_ms']>=3000,'Short final released observation')
    return dict(status='PASS',full_pairs=len(work),gpio_groups=sum(value for value,_ in groups),**stacks(rows))


def validate_link_loss(rows,peer,source,requested,acknowledged,host_down_at,errno_abi):
    text='\n'.join(line for _,line in rows)
    require(rows and rows[0][1].startswith('*** Booting Zephyr') and
            0<=rows[-1][0]-rows[0][0]<=120 and text.count('*** Booting Zephyr')==1 and
            text.count(f'M3_BOOT source={source} profile=integrated commands=1 fault_default=off')==1,
            'Wrong/fresh bounded link-loss composition capture')
    require(not re.search(r'FATAL ERROR|ASSERTION FAIL|M3_WDT_|M3_\w*FAULT|M3_\w*REJECTED',text),
            'Unexpected link-loss reset/watchdog/fault/rejection')
    windows=records(rows,'M3_NET_WINDOW');stops=records(rows,'M3_NET_WINDOW_STOP')
    allowed={-errno_abi[name] for name in ('ENETDOWN','ENETUNREACH','ECONNRESET','ENOTCONN',
                                        'ETIMEDOUT','ECONNREFUSED','EHOSTUNREACH')}
    require(len(windows)==len(stops)==1 and 59999<=windows[0]['end_ms']-windows[0]['started_ms']<=60000 and
            stops[0]['result'] in allowed and stops[0]['monotonic_ms']<windows[0]['end_ms'] and
            requested<=stops[0]['received']<=acknowledged+15,
            'Window expiry or unrelated error substituted for physical loss')
    start=next(i for i,(_,line) in enumerate(rows) if 'M3_NET_WINDOW ' in line)
    stop=next(i for i,(_,line) in enumerate(rows) if 'M3_NET_WINDOW_STOP ' in line)
    default_state(rows[:start],source,1);idle_resources(rows[:start])
    require(requested<=acknowledged<=requested+90 and 0<=host_down_at-acknowledged<=15,
            'Operator receipt or fresh host link-down bounds failed')
    state=records(rows,'M3_NET_STATE');observers=records(rows,'M3_OBSERVER_ALIVE')
    prior=[r for r in observers if r['received']<=acknowledged]
    require(prior and windows[0]['end_ms']-(prior[-1]['monotonic_ms']+
            (acknowledged-prior[-1]['received'])*1000)>=25000, 'Cable acknowledgement too late for original window')
    require(any(r['link']==r['service_ready']==0 and requested<=r['received']<=acknowledged+15
                for r in state), 'No actual native carrier/service loss after instruction')
    before_instruction=[r for r in observers if r['received']<=requested]
    carrier_before=[r for r in state if r['received']<=requested]
    require(before_instruction and carrier_before and
            before_instruction[-1]['healthy']==before_instruction[-1]['readiness']==1 and
            before_instruction[-1]['network_active']==1 and requested-before_instruction[-1]['received']<=1.25 and
            carrier_before[-1]['link']==carrier_before[-1]['service_ready']==1 and
            requested-carrier_before[-1]['received']<=16.25,
            'Already unhealthy or stale before cable instruction')
    require(not any(r['result']<0 and r['received']<requested for r in records(rows,'M3_NET_EXCHANGE')),
            'Network failure predates cable instruction')
    require(any(r['readiness']==r['healthy']==0 and acknowledged<=r['received']<=acknowledged+15 and
                r['monotonic_ms']<windows[0]['end_ms'] for r in observers),
            'No fresh unready observation before expiry')
    complete=records(rows,'M3_NET_WORK')
    require(complete and [r['sequence'] for r in complete]==list(range(1,len(complete)+1)),
            'No ordered actual combined progress before loss')
    exchanges=records(rows,'M3_NET_EXCHANGE')
    expected=[(p,n) for n in range(1,len(complete)+1) for p in (1,2)]
    successful=[r for r in exchanges if r['sequence']<=len(complete)]
    require([(r['protocol'],r['sequence']) for r in successful]==expected and all(
        r['trial']==1 and r['result']==0 and 0<=r['elapsed_ms']<=2250 for r in successful),
        'Completed pair identity/status/budget inconsistent')
    # Qualify completed pairs from original rows; the final failed prefix remains separate.
    for work in complete:
        indices=[i for i,(_,line) in enumerate(rows) if 'M3_NET_EXCHANGE ' in line and
                 records([(0,line)],'M3_NET_EXCHANGE')[0]['sequence']==work['sequence']]
        finished=next(i for i,(_,line) in enumerate(rows) if 'M3_NET_WORK ' in line and
                      records([(0,line)],'M3_NET_WORK')[0]['sequence']==work['sequence'])
        require(len(indices)==2 and indices[0]<indices[1]<finished,'Completed pair event order')
        for left,right in ((indices[0],indices[1]),(indices[1],finished)):
            returned=records(rows[left+1:right],'M3_NET_CLEANUP')
            require(len(returned)==1 and returned[0]['result']==0 and
                    returned[0]['expected_contexts']==returned[0]['observed_contexts']==0 and
                    returned[0]['restart_required']==0,'Completed pair lacks cleanup return')
    prefix=[r for r in exchanges if r['sequence']>len(complete)]
    require(1<=len(prefix)<=2 and [r['protocol'] for r in prefix]==[1,2][:len(prefix)] and all(
        r['trial']==1 and r['sequence']==len(complete)+1 and
        0<=r['elapsed_ms']<=2250 and r['result'] in allowed|{0} for r in prefix) and
        prefix[-1]['result'] in allowed and all(r['result']==0 for r in prefix[:-1]),
        'Missing/unbounded/wrong failed pair prefix')
    require(requested<=prefix[-1]['received']<=acknowledged+15 and
            prefix[-1]['monotonic_ms']<windows[0]['end_ms'], 'Failure predates instruction or original window')
    require(not records(rows[stop+1:],'M3_NET_EXCHANGE') and not records(rows[stop+1:],'M3_NET_WORK'),
            'New work after inactive publication')
    cleanup=records(rows,'M3_NET_CLEANUP')
    require(cleanup and all(r['expected_contexts']==0 and 0<=r['elapsed_ms']<=5250 and
            ((r['result']==0 and r['observed_contexts']==r['restart_required']==0) or
             (r['result']==-errno_abi['ETIMEDOUT'] and r['observed_contexts']<=1 and r['restart_required']==1))
            for r in cleanup), 'Native cleanup baseline/budget/result invalid')
    fences=[i for i,(_,line) in enumerate(rows) if 'M3_NET_CLEANUP ' in line and 'restart_required=1' in line]
    if fences:
        require(len(fences)==1 and not records(rows[fences[0]+1:],'M3_NET_EXCHANGE') and
                stops[0]['result']==-errno_abi['ETIMEDOUT'],'New work or wrong result after cleanup fence')
    resources=records(rows,'M3_NET_RESOURCE')
    require(resources and all(r['owned']<=1 and r['contexts']<=1 and
            0<=r['rx_pkt_free']<=16 and 0<=r['tx_pkt_free']<=16 and
            0<=r['rx_buf_free']<=32 and 0<=r['tx_buf_free']<=32 for r in resources) and
            resources[-1]['owned']==0,'Unbounded native ownership/pools')
    require(all(r['event'] in ('peer_ready','peer_closed','exchange','rejected') for r in peer) and
            sum(r['event']=='peer_ready' for r in peer)==sum(r['event']=='peer_closed' for r in peer)==1,
            'Unknown/duplicate peer lifecycle')
    require(peer and peer[0]['event']=='peer_ready' and peer[-1]['event']=='peer_closed' and
            peer[0].get('clock')=='clock_gettime(CLOCK_MONOTONIC)' and
            peer[0]['monotonic']<=exchanges[0]['received']<=requested<=stops[0]['received']<=peer[-1]['monotonic'],
            'Peer identity/clock/lifetime mismatch')
    frames=[r for r in peer if r['event']=='exchange'];rejected=[r for r in peer if r['event']=='rejected']
    require(peer[-1]['exchanges']==len(frames)+len(rejected) and len(rejected)<=1 and all(
        r['peer'][0]=='192.168.77.2' and r['received_bytes']<32 and
        r['error'] in ('Truncated TCP frame','timed out') and r['monotonic']>=requested for r in rejected),
        'Unexpected peer rejection/count')
    received=[r for r in frames if r['sequence']<=len(complete)]
    require([(1 if r['protocol']=='tcp' else 2,r['sequence']) for r in received]==expected,
            'Both-end completed pairs disagree')
    extra=[r for r in frames if r['sequence']>len(complete)]
    require(len(extra)<=2 and [1 if r['protocol']=='tcp' else 2 for r in extra]==[1,2][:len(extra)] and all(
        r['sequence']==len(complete)+1 for r in extra),'Unbounded peer prefix after completed pairs')
    require(all(r['trial']==1 and r['received_bytes']==32 and r['peer'][0]=='192.168.77.2' and
                r['mode']=='echo' and r['response']=='sent' for r in frames),'Wrong physical peer frame')
    return dict(status='PASS',full_pairs_before_loss=len(complete),in_place_cleanup_failed=bool(fences),
                recovery='one preselected explicit restart',**stacks(rows))

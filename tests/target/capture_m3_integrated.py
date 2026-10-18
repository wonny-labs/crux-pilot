"""Bounded reviewed combined physical series. Not a product runtime or unattended retry loop."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import termios
import traceback
R=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(R/'tests/target'))
import m3_clock as time
import capture_m3_network as net
from capture_m3 import Receiver, HardwareStop, commander, drain, select_uart, verify_manifest, sha, check_owners
from m3_health_oracle import require
from m3_image import check_record_output
from m3_errno import verify_errno_map
from capture_m3_gpio import speak, spoken_cue
from m3_network_oracle import records
from m3_integrated_oracle import (default_state, pairs, stacks, validate_watchdog,
                                  validate_functional, validate_link_loss, idle_resources)
from m3_integrated_output import logger_groups, idle_logger_checkpoint, retained_prefix, planned_reset_rows


class FunctionalFailure(ValueError): pass


def functional_require(condition,message):
    if not condition:raise FunctionalFailure(message)


def initial_rows(rows, source, commands):
    banners=[i for i,(_,line) in enumerate(rows) if line.startswith('*** Booting Zephyr')]
    require(len(banners)<=1,'Duplicate native boot in initial composition phase')
    if not banners:return []
    fresh=rows[banners[0]:]
    boots=records(fresh,'M3_BOOT')
    require(len(boots)<=1,'Duplicate initial composition identity')
    if not boots:return []
    require(0<=boots[0]['received']-fresh[0][0]<=.5 and
            boots[0]['source']==source and boots[0]['profile']=='integrated' and
            boots[0]['commands']==commands and boots[0]['fault_default']=='off',
            'Wrong/late initial composition identity')
    return fresh


def initial_checkpoint_rows(rows, source, commands, native_output=False):
    fresh=initial_rows(rows,source,commands)
    observers=[i for i,(_,line) in enumerate(fresh) if 'M3_OBSERVER_ALIVE ' in line]
    if len(observers)<5:return []
    fifth=observers[4]
    stacks(fresh[:fifth])
    upper=observers[5] if len(observers)>5 else len(fresh)
    paired=[i for i in range(fifth+1,upper) if 'M3_STACK ' in fresh[i][1]]
    if not paired and len(observers)==5:return []
    require(len(paired)==1,'Missing/duplicate fifth initial stack')
    last=paired[0]
    if native_output:
        loggers=[i for i in range(last+1,upper) if fresh[i][1].startswith('M3_LOGGER ')]
        if not loggers and len(observers)==5:return []
        require(len(loggers)==1,'Missing/duplicate fifth native logger')
        last=loggers[0]
    checkpoint=fresh[:last+1]
    stacks(checkpoint)
    if native_output:idle_logger_checkpoint(checkpoint)
    return checkpoint


def native_handoff_identity(image):
    """Bind the qualified driver and transport/output configuration, excluding application roles."""
    config={}
    for line in Path(image['config']).read_text().splitlines():
        if line.startswith('# CONFIG_') and line.endswith(' is not set'):
            key,value=line[2:-11],'n'
        elif line.startswith('CONFIG_') and '=' in line:
            key,value=line.split('=',1)
        else:continue
        if key.startswith(('CONFIG_ETH_', 'CONFIG_PHY_', 'CONFIG_MDIO_', 'CONFIG_NET_', 'CONFIG_LOG_')) or key in (
                'CONFIG_CRUX_M3_MDIO_COMPLETION_FIX','CONFIG_CRUX_M3_NATIVE_OUTPUT','CONFIG_PRINTK_SYNC'):
            config[key]=value
    patch=image.get('native_patch',{})
    identity={k:patch.get(k) for k in ('upstream_sha256','patch_sha256','generated_sha256')}
    require(all(re.fullmatch(r'[0-9a-f]{64}',v or '') for v in identity.values()),
            'Missing qualified native patch identity')
    return dict(config=config,patch=identity)


def normal_native_differences(probe,normal):
    """Record only the pinned logger/Shell consequences of disabling development commands."""
    left,right=native_handoff_identity(probe),native_handoff_identity(normal)
    require(left['patch']==right['patch'], 'Integrated normal driver patch differs')
    differences={key:(left['config'].get(key),right['config'].get(key))
                 for key in set(left['config'])|set(right['config'])
                 if left['config'].get(key)!=right['config'].get(key)}
    require(differences=={'CONFIG_LOG_CMDS':('n',None),
                         'CONFIG_LOG_RUNTIME_DEFAULT_LEVEL':('0',None),
                         'CONFIG_MDIO_SHELL':('n',None),
                         'CONFIG_LOG_RUNTIME_FILTERING':('y','n')},
            'Unexpected integrated normal transport/output difference')
    require('CONFIG_SHELL=y' in Path(probe['config']).read_text().splitlines() and
            '# CONFIG_SHELL is not set' in Path(normal['config']).read_text().splitlines(),
            'Expected development Shell disablement absent')
    return differences


def verify_network_handoff(path, probe):
    handoff=json.loads(path.read_text())
    require(handoff['status']=='PASS' and handoff['physical_stages']==7 and
            sha(handoff['review_path'])==handoff['review_sha256'] and
            sha(handoff['result_path'])==handoff['result_sha256'] and
            sha(handoff['manifest_path'])==handoff['manifest_sha256'],
            'Physical #11 handoff evidence changed')
    network=verify_manifest(Path(handoff['manifest_path']))
    require(network['profile']=='network' and network['west_projects']==probe['west_projects'],
            '#11 and integration pinned native dependencies differ')
    component_hashes={}
    for relative in ('samples/controller/zephyr/network_client.c','samples/controller/zephyr/network_client.h',
                     'samples/controller/zephyr/m3-network.overlay','samples/controller/zephyr/native_logger.c',
                     'samples/controller/zephyr/native_logger.h','patches/zephyr/nxp-enet-mdio-completion.patch'):
        blobs=[]
        for image in (network,probe):
            commit=image['source']['git_commit']
            require(re.fullmatch(r'[0-9a-f]{40}',commit),'Invalid component source commit')
            blobs.append(subprocess.check_output(['git','show',commit+':'+relative],cwd=R,timeout=5))
        require(blobs[0]==blobs[1],'Qualified network component changed: '+relative)
        import hashlib
        component_hashes[relative]=hashlib.sha256(blobs[0]).hexdigest()
    require(handoff['component_source_sha256']==component_hashes,
            'Network component handoff hashes differ')
    qualified=native_handoff_identity(network)
    require(handoff.get('native_identity')==qualified and native_handoff_identity(probe)==qualified,
            'Qualified native transport/PHY/output configuration or patch changed')
    result_path=Path(handoff['result_path']);root=result_path.parent
    result=json.loads(result_path.read_text())
    inputs=json.loads((root/'inputs.json').read_text())
    require(result['status']=='PASS' and result['stages']==7 and inputs['image']==network,
            '#11 physical result is not bound to qualified network image')
    for stage in range(1,8):
        require(json.loads((root/f'stage-{stage}/result.json').read_text())['status']=='PASS',
                'Missing physical Ethernet stage')
    require(json.loads((root/'final-normal-restoration/result.json').read_text())['status']=='PASS',
            '#11 final normal restoration absent')
    return handoff


class Series:
    def __init__(self,args):
        self.args=args;self.directory=args.evidence
        self.directory.mkdir(exist_ok=False)
        self.end=time.monotonic()+1800
        self.case_end=self.end
        net.SERIES_DEADLINE=self.end
        self.guard=json.loads(args.guard_session.read_text())
        self.images={role:verify_manifest(path) for role,path in (
            ('probe',args.probe_manifest),('normal',args.normal_manifest),('original',args.original_manifest))}
        require(self.images['probe']['profile']=='integrated-probe' and
                self.images['normal']['profile']=='integrated-normal' and
                self.images['probe']['source']==self.images['normal']['source'],'Wrong combined image roles/source')
        require(sha(self.images['original']['binary'])=='238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0',
                'Original normal changed')
        handoff=verify_network_handoff(args.network_handoff,self.images['probe'])
        for role in ('probe','normal'):check_record_output(self.images[role]['config'])
        role_differences=normal_native_differences(self.images['probe'],self.images['normal'])
        self.native_output={role:'CONFIG_CRUX_M3_NATIVE_OUTPUT=y' in
                            Path(self.images[role]['config']).read_text().splitlines()
                            for role in ('probe','normal')}
        require(self.native_output['probe']==self.native_output['normal'], 'Combined output roles differ')
        self.current_role='original'
        self.abi=verify_errno_map(args.errno_map,self.images['probe'])
        audio=json.loads(args.audio_confirmation.read_text())
        require(audio.get('voice')=='Yuna' and audio.get('heard') is True and
                0<=time.monotonic()-audio['confirmed_at']<=900,'No fresh actual Yuna audibility confirmation')
        self.audio=audio
        self.source=self.images['probe']['source']['git_commit'][:12]
        self.lease=dict(process=None,log=None)
        (self.directory/'inputs.json').write_text(json.dumps(dict(images=self.images,network_handoff=handoff,
            guard=self.guard,clock=time.identity(),audio_confirmation=self.audio,errno_abi=self.abi,errno_map_sha256=sha(args.errno_map),native_role_differences=role_differences,runner_git_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip(),scripts={str(p):sha(p) for p in [Path(__file__),Path(__file__).with_name('m3_integrated_oracle.py')]+sorted((R/'tests/target').glob('*.py'))}),indent=2))

    def target(self,commands,label):
        if time.monotonic()+120>=self.end or time.monotonic()+45>=self.case_end:
            raise HardwareStop('Insufficient series/case inventory and probe reserve')
        net.guard_check(self.guard,self.directory,label)
        if time.monotonic()+120>=self.end or time.monotonic()+45>=self.case_end:
            raise HardwareStop('Probe reserve expired during guard inventory')
        try:
            text=commander('602003106',commands,self.directory/(label+'.log'))
            voltage=re.search(r'VTref\s*=\s*([\d.]+)\s*V',text)
            require(voltage and 3<=float(voltage[1])<=3.6 and 'CPUID register: 0x411FC271' in text and
                    'DPIDR: 0x0BD11477' in text and re.search(r'S/N:\s*602003106\b',text),'Wrong target/probe/voltage')
            return text
        except HardwareStop:raise
        except Exception as error:raise HardwareStop('Target identity/evidence uncertain') from error

    def wait(self,predicate,duration,receiver=None):
        net.wait_until(predicate,min(duration,max(0,self.case_end-time.monotonic())),receiver)

    def send(self,port,receiver,command,directory):
        try:
            require(time.monotonic()+20<self.end and receiver.failure is None,'Command outside healthy transport/budget')
            data=(command+'\r').encode()
            require(port.write(data)==len(data),'Incomplete command write')
            with (directory/'commands.jsonl').open('a') as output:
                output.write(json.dumps(dict(command=command,monotonic=time.monotonic()))+'\n')
        except Exception as error:raise HardwareStop('UART command/record failed') from error

    def peer(self,case,duration):
        require(net.check_route(case,'peer-entry'),'Dedicated link inactive')
        self.lease['log']=(case/'peer-process.log').open('wb')
        self.lease['process']=subprocess.Popen([sys.executable,str(R/'tests/target/m3_network_peer.py'),
            '--duration',str(duration),'--mode','echo','--output',str(case/'peer.jsonl')],
            stdout=self.lease['log'],stderr=subprocess.STDOUT,start_new_session=True)
        try:self.wait(lambda:(case/'peer.jsonl').exists() and 'peer_ready' in (case/'peer.jsonl').read_text(),3)
        except Exception as error:raise HardwareStop('Peer startup failed') from error

    def operator(self,action):
        path=self.directory/(action+'.json')
        requested=time.monotonic()
        deadline=min(self.end,self.case_end,requested+90)
        (self.directory/'operator-request.json').write_text(json.dumps(dict(action=action,requested=requested,deadline=deadline)))
        print('OPERATOR_ACTION '+action,flush=True)
        self.wait(path.exists,90)
        reply=json.loads(path.read_text())
        require(reply.get('action')==action and reply.get('confirmed') is True,'Operator action not confirmed')
        event=dict(action=action,requested=requested,acknowledged=time.monotonic(),deadline=deadline,confirmation=reply)
        with (self.directory/'operator-events.jsonl').open('a') as output:output.write(json.dumps(event)+'\n')
        return event

    def capture(self,name,operation,body,role='probe'):
        require(time.monotonic()+(260 if role=='probe' else 140)<self.end,'Insufficient case and normal-recovery reserve')
        self.case_end=min(self.end,time.monotonic()+(90 if name.startswith('watchdog-') else 120 if name.startswith('F2-') else 80))
        case=self.directory/name;case.mkdir()
        entry_role=getattr(self,'current_role','original')
        drain(self.port,case)
        receiver=Receiver(self.port,case);receiver.begin()
        try:
            image=self.images[role]
            if operation=='program':
                binary=image['binary']
                text=self.target(net.image_commands(binary,True),name)
                if 'Verify successful' not in text:raise HardwareStop('Full BIN verify absent')
            else:self.target('r\ng\nq\n',name)
            self.wait(lambda:f'M3_BOOT source={self.source} profile=integrated commands={int(role=="probe")} fault_default=off' in receiver.text(),10,receiver)
            self.current_role=role
            baseline_box=[]
            def initial_ready():
                candidate_rows=list(receiver.rows)
                if getattr(self,'native_output',{}).get(role,False):
                    candidate_rows,_ = planned_reset_rows(candidate_rows)
                candidate=initial_checkpoint_rows(candidate_rows,self.source,int(role=='probe'),
                                                  getattr(self,'native_output',{}).get(role,False))
                if not candidate:return False
                baseline_box.append(candidate)
                return True
            self.wait(initial_ready,10,receiver)
            banners=[i for i,(_,s) in enumerate(baseline_box[0]) if s.startswith('*** Booting Zephyr')]
            require(len(banners)==1,'Initial fresh boot absent/duplicated')
            baseline=baseline_box[0]
            default_state(baseline,self.source,int(role=='probe'))
            (case/'baseline-result.json').write_text(json.dumps(dict(status='PASS'),indent=2))
            result=body(case,receiver) if body else None
        finally:
            try:net.finish_receiver(receiver)
            finally:
                try:
                    if getattr(self,'native_output',{}).get(role,False):
                        from m3_uart import exact_event_rows
                        try:
                            receiver.rows,excluded=planned_reset_rows(exact_event_rows(case))
                            (case/'planned-reset-boundary.json').write_text(json.dumps(dict(
                                excluded_old_fragment=excluded,raw_unchanged=True),indent=2)+'\n')
                            banners=[i for i,(_,s) in enumerate(receiver.rows)
                                     if s.startswith('*** Booting Zephyr')]
                            require(banners,'Missing exact fresh boot')
                            prefix=receiver.rows[:banners[0]]
                            fragment=retained_prefix(prefix,entry_role)
                            (case/'pre-reset-prefix.json').write_text(json.dumps(dict(
                                role=entry_role,
                                right_censored_normal_fragment=fragment,
                                excluded_from_qualification=True),indent=2)+'\n')
                            fresh=receiver.rows[banners[0]:]
                            logger_groups(fresh)
                            first_epoch=fresh[:next((i for i in range(1,len(fresh))
                                if fresh[i][1].startswith('*** Booting Zephyr')),len(fresh))]
                            idle_logger_checkpoint(initial_checkpoint_rows(first_epoch,self.source,
                                                   int(role=='probe'),True))
                        except Exception as error:
                            raise HardwareStop('Exact integrated UART/native output evidence uncertain') from error
                finally:
                    (case/'lines.json').write_text(json.dumps(receiver.rows,indent=2))
                    try:net.release_peer(self.lease)
                    finally:self.case_end=self.end
        self.current_role=role
        if body is None:
            banners=[i for i,(_,line) in enumerate(receiver.rows) if line.startswith('*** Booting Zephyr')]
            require(len(banners)==1,'Final normal fresh boot absent/duplicated')
            frozen=receiver.rows[banners[0]:]
            result=default_state(frozen,self.source,0)
            result.update(idle_resources(frozen))
        return case,receiver,result

    def start_window(self,case,receiver):
        require(time.monotonic()+65<self.case_end,'Insufficient case budget for the original full window')
        self.send(self.port,receiver,'m3 network',case)
        self.wait(lambda:len(records(receiver.rows,'M3_NET_WINDOW'))==1,2,receiver)
        self.wait(lambda:len([r for r in records(receiver.rows,'M3_OBSERVER_ALIVE') if r['readiness']==1])>=2,18,receiver)
        window=records(receiver.rows,'M3_NET_WINDOW')[0]
        return window

    def voice(self,case,receiver):
        started=time.monotonic()
        ready=started+2
        for index in range(10):
            scheduled=ready+4*index
            self.wait(lambda:time.monotonic()>=scheduled,max(0,scheduled-time.monotonic())+1,receiver)
            spoken_cue(index,ready,case)
        self.wait(lambda:time.monotonic()>=started+45,8,receiver)
        ended=time.monotonic()
        (case/'cues.json').write_text(json.dumps(dict(started=started,ended=ended,voice='Yuna',
            phrases=['누르세요','놓으세요'],events='spoken-cues.jsonl'),indent=2))
        return started,ended

    def watchdog_body(self,owner):
        def run(case,receiver):
            self.peer(case,100)
            self.start_window(case,receiver)
            self.send(self.port,receiver,'m3 arm',case)
            self.wait(lambda:bool(records(receiver.rows,'M3_WDT_ARMED')),2,receiver)
            arm=records(receiver.rows,'M3_WDT_ARMED')[0]
            self.wait(lambda:time.monotonic()>=arm['received']+12.25,13,receiver)
            feeds=records(receiver.rows,'M3_WDT_FEED')
            functional_require(feeds and feeds[-1]['monotonic_ms']-feeds[0]['monotonic_ms']>=12000 and
                    len([r for r in records(receiver.rows,'M3_NET_WORK') if r['received']>=arm['received']])>=2,
                    'Fixed healthy checkpoint failed; no extension')
            self.send(self.port,receiver,'m3 stall' if owner=='time' else 'm3 stall_network',case)
            marker='M3_FAULT_STALL' if owner=='time' else 'M3_NET_FAULT_STALL'
            self.wait(lambda:bool(records(receiver.rows,marker)) or 'REJECTED' in receiver.text(),16,receiver)
            functional_require('REJECTED' not in receiver.text(),'Owner rejected bounded fault; no retry')
            self.wait(lambda:len([s for _,s in receiver.rows if s.startswith('*** Booting Zephyr')])==2,23,receiver)
            reset=next(t for t,s in receiver.rows if s.startswith('*** Booting Zephyr') and t>receiver.rows[0][0]+1)
            self.wait(lambda:time.monotonic()>=reset+4.5,5,receiver)
            return dict(status='CAPTURED',owner=owner)
        return run

    def healthy_body(self,gpio=False):
        def run(case,receiver):
            self.peer(case,100)
            window=self.start_window(case,receiver)
            interval=None
            if gpio:
                latest=records(receiver.rows,'M3_OBSERVER_ALIVE')[-1]
                require(window['end_ms']-latest['monotonic_ms']>=45000,'Insufficient original window for fixed GPIO stimulus')
                require(time.monotonic()+45<self.case_end,'Insufficient case budget for fixed GPIO cues')
                interval=self.voice(case,receiver)
            self.wait(lambda:bool(records(receiver.rows,'M3_NET_WINDOW_STOP')),65,receiver)
            stopped=records(receiver.rows,'M3_NET_WINDOW_STOP')[0]
            self.wait(lambda:time.monotonic()>=stopped['received']+3.25,4,receiver)
            return dict(status='CAPTURED',gpio_interval=interval)
        return run

    def loss_body(self,case,receiver):
        self.peer(case,180)
        window=self.start_window(case,receiver)
        operator=self.operator('integrated-unplug-evkb-rj45')
        acknowledged=operator['acknowledged']
        requested=operator['requested']
        latest=records(receiver.rows,'M3_OBSERVER_ALIVE')[-1]
        estimated_now=latest['monotonic_ms']+1000*(acknowledged-latest['received'])
        functional_require(window['end_ms']-estimated_now>=26000,'Cable action too late for original window; no substitute expiry')
        host_down=[None]
        def down():
            if not net.check_link(case,'physical-link-down'):
                host_down[0]=time.monotonic()
                return True
            return False
        self.wait(down,15,receiver)
        self.wait(lambda:any(r['link']==r['service_ready']==0 and r['received']>=requested
                            for r in records(receiver.rows,'M3_NET_STATE')),15,receiver)
        self.wait(lambda:bool(records(receiver.rows,'M3_NET_WINDOW_STOP')),15,receiver)
        stopped=records(receiver.rows,'M3_NET_WINDOW_STOP')[0]
        self.wait(lambda:time.monotonic()>=stopped['received']+3.25,4,receiver)
        # Loss may already have completed while the operator confirms it.
        # Still collect a fresh post-receipt observation for the unchanged oracle.
        self.wait(lambda:any(r['readiness']==r['healthy']==0 and
                            acknowledged<=r['received']<=acknowledged+15 and
                            r['monotonic_ms']<window['end_ms']
                            for r in records(receiver.rows,'M3_OBSERVER_ALIVE')),
                  max(0,acknowledged+15-time.monotonic()),receiver)
        return dict(status='CAPTURED',requested=requested,acknowledged=acknowledged,host_down_at=host_down[0])

    def qualify(self,case,receiver,metadata,kind,owner=None):
        banners=[i for i,(_,line) in enumerate(receiver.rows) if line.startswith('*** Booting Zephyr')]
        require(banners,'Missing fresh native boot')
        rows=receiver.rows[banners[0]:]
        peer=[json.loads(line) for line in (case/'peer.jsonl').read_text().splitlines()]
        try:
            if kind=='watchdog':result=validate_watchdog(rows,peer,self.source,owner)
            elif kind=='loss':result=validate_link_loss(rows,peer,self.source,metadata['requested'],metadata['acknowledged'],metadata['host_down_at'],self.abi)
            else:result=validate_functional(rows,peer,self.source,metadata.get('gpio_interval'))
            result['preserved_preboot_rows']=banners[0]
        except ValueError as error:
            raise FunctionalFailure(str(error)) from error
        (case/'result.json').write_text(json.dumps(result,indent=2))
        print('CASE_PASS '+case.name,flush=True)
        return result

    def execute(self):
        speak(['-o',str(self.directory/'cue-preflight.aiff'),'누르세요 놓으세요'],
              5,self.directory/'cue-preflight.log')
        net.guard_check(self.guard,self.directory,'entry',1850)
        require(not subprocess.check_output(['git','status','--porcelain'],cwd=R,text=True).strip(),'Source tree not frozen')
        import serial
        import serial.tools.list_ports
        with (self.directory.parent/'evkb-602003106.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            check_owners(self.args.uart,self.directory/'owners.txt')
            select_uart([vars(p) for p in serial.tools.list_ports.comports()],self.args.uart,net.USB_SERIAL)
            with serial.Serial(self.args.uart,115200,timeout=.02,exclusive=True,write_timeout=1) as self.port:
                fcntl.ioctl(self.port.fileno(),termios.TIOCEXCL)
                self.target('q\n','entry-identity')
                original=self.images['original']['binary']
                if 'Verify successful' not in self.target(f'verifybin "{original}", 0x60000000\nq\n','entry-known-normal'):
                    raise HardwareStop('Known current normal differs')
                try:
                    case,rx,meta=self.capture('F1-gpio-window','program',self.healthy_body(True))
                    self.qualify(case,rx,meta,'healthy')
                    case,rx,meta=self.capture('F2-physical-link-loss','reset',self.loss_body)
                    self.qualify(case,rx,meta,'loss')
                    self.operator('integrated-reconnect-evkb-rj45')
                    self.wait(lambda:net.check_link(self.directory,'restored-link'),15)
                    time.sleep(1)
                    require(net.check_route(self.directory,'restored-route'),'Restored dedicated link inactive')
                    case,rx,meta=self.capture('F3-explicit-link-recovery','reset',self.healthy_body())
                    self.qualify(case,rx,meta,'healthy')
                    for owner in ('time','network'):
                        for trial in range(1,4):
                            case,rx,meta=self.capture(f'watchdog-{owner}-{trial}','reset',self.watchdog_body(owner))
                            self.qualify(case,rx,meta,'watchdog',owner)
                except FunctionalFailure:
                    net.release_peer(self.lease)
                    self.capture('failed-qualification-normal-restoration','program',None,'normal')
                    raise
                except Exception as error:
                    net.release_peer(self.lease)
                    if isinstance(error,HardwareStop):raise
                    raise HardwareStop('Unknown transport/observation state; no automatic restoration') from error
                else:
                    case,rx,result=self.capture('final-integrated-normal','program',None,'normal')
                    (case/'result.json').write_text(json.dumps(result,indent=2))
                    (self.directory/'result.json').write_text(json.dumps(dict(status='PASS',functional_cases=3,watchdog_trials=6,
                        final_state='identified integrated-normal, unarmed, no Shell/fault commands/automatic traffic'),indent=2))



def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for option in ('probe-manifest','normal-manifest','original-manifest','network-handoff','guard-session','evidence','errno-map','audio-confirmation'):
        parser.add_argument('--'+option,type=Path,required=True)
    parser.add_argument('--uart',default='/dev/cu.usbmodem11302')
    args=parser.parse_args()
    series=Series(args)
    try:series.execute()
    except BaseException:
        (args.evidence/'failure.txt').write_text(traceback.format_exc());raise


if __name__=='__main__':main()

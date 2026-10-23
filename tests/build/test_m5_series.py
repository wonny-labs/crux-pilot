"""Full fresh-boot M5 evidence, including independent host loss witnesses."""
import copy
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
import m5_series
ABI={'ETIMEDOUT':116,'ECONNRESET':104,'ENETDOWN':115,'ENETUNREACH':114}
SOURCE='123456789abc'

def complete():
    boots=[]
    for boot_number in range(3):
        token=71+boot_number;host=boot_number*500.+1;target=0;uart=[];peers={};controls=[];peer_stops=[];writes=[dict(monotonic=host+.0005,line="A "+str(token)+"\n")]
        def put(row):
            nonlocal host
            host+=.001;uart.append(dict(monotonic=host,record=row))
        put(dict(event='BOOT',source=SOURCE))
        writes[0]['monotonic']=host+.0005
        put(dict(event='ARM',token=token,at=target))
        put(dict(event='READY',token=token,seq=1,ticket=301,at=target))
        put(dict(event='LINK',token=token,seq=0,at=0,link=2,admin=1,address=1,ipv4=3232255234))
        for seq,count,mode in m5_series.PHASES:
            action='I' if not count else 'L' if seq==7 else 'P'
            writes.append(dict(monotonic=host+.0005,line=' '.join([action,str(token),str(seq)]+([str(count)] if action=='P' else [])+[str(300+seq)])+'\n'))
            if seq==8:
                controls.extend([dict(event='cable_connect_receipt',monotonic=host+.001),dict(event='host_up',monotonic=host+.002,interface='en5'),dict(event='route',monotonic=host+.003,interface='en5',source='192.168.77.1',destination='192.168.77.2')]);host+=.004
                put(dict(event='LINK',token=token,seq=7,at=target,link=2,admin=1,address=1,ipv4=3232255234))
                writes[-1]['monotonic']=host+.0005
            if count:
                peer=[dict(event='ready',clock='clock_gettime(CLOCK_MONOTONIC)',monotonic=host+.0001,token=token,phase=seq,mode=mode,address='192.168.77.1',tcp_port=42420,udp_port=42421)]
                for number in range(count):
                    for udp in (0,1):
                        put(dict(event='BEGIN',token=token,seq=seq,round=number,udp=udp,at=target))
                        frame=struct.pack('!4sIHHI16s',b'M5N1',token,seq,2 if udp else 1,number+1,b'crux-m5-echo-v1\0').hex()
                        if mode!='loss' or not udp:
                            peer.append(dict(event='request',clock='clock_gettime(CLOCK_MONOTONIC)',monotonic=host+.001,token=token,phase=seq,protocol='udp' if udp else 'tcp',transaction=number+1,request_hex=frame))
                            if mode=='loss':
                                controls.extend([dict(event='cable_disconnect_cue',monotonic=host+.002),dict(event='cable_disconnect_receipt',monotonic=host+.003),dict(event='host_down',monotonic=host+.004,interface='en5')]);host+=.005
                                put(dict(event='LINK',token=token,seq=seq,at=target+1,link=1,admin=1,address=1,ipv4=3232255234))
                            peer.append(dict(event='exchange',clock='clock_gettime(CLOCK_MONOTONIC)',monotonic=host+.001,token=token,phase=seq,protocol='udp' if udp else 'tcp',transaction=number+1,request_hex=frame,response_hex=frame if mode=='echo' or (mode=='close' and udp) else ''))
                        host+=.002;target+=2000 if mode in ('silent','loss') else 10
                        result=0 if mode=='echo' or (mode=='close' and udp) else -104 if mode=='close' else -116
                        put(dict(event='END',token=token,seq=seq,round=number,udp=udp,at=target,result=result,entered=target-(2000 if mode in ('silent','loss') else 10),functional=target,close_error=0,peak=1,owned=0))
                        for _ in range(3):
                            put(dict(event='IDLE',token=token,seq=seq,checkpoint=number*2+udp,at=target,contexts=0,rx_pkt=16,tx_pkt=16,rx_buf=32,tx_buf=32,main_free=1600,observer_free=800));target+=1000
                peers[str(seq)]=peer
            else:
                for _ in range(3):
                    put(dict(event='IDLE',token=token,seq=seq,checkpoint=0,at=target,contexts=0,rx_pkt=16,tx_pkt=16,rx_buf=32,tx_buf=32,main_free=1600,observer_free=800));target+=1000
            put(dict(event='DONE',token=token,seq=seq,at=target))
            put(dict(event='READY',token=token,seq=seq+1,ticket=301+seq,at=target))
            if count:peer_stops.append(dict(phase=seq,reaped=True,monotonic=host+.00001))
        boots.append(dict(nonce=token,reset=dict(operation='qualification_reset_'+str(boot_number+1),monotonic=boot_number*500.),uart=uart,peers=peers,controls=controls,writes=writes,peer_stops=peer_stops))
    return dict(schema=1,source=SOURCE,clock='clock_gettime(CLOCK_MONOTONIC)',boots=boots)

class Series(unittest.TestCase):
    def test_pass_is_explicitly_not_physical_acceptance(self):
        result=m5_series.validate_series(complete(),SOURCE,ABI)
        self.assertIn('physical_acceptance',result)
        self.assertIs(result['physical_acceptance'],False)
        self.assertEqual(result['scope'],'communication_sequence_only')

    def test_complete_three_boot_series(self):
        result=m5_series.validate_series(complete(),SOURCE,ABI)
        self.assertEqual((result['status'],result['boots'],result['calls']),('PASS',3,96))
    def test_missing_duplicate_misordered_and_incomplete(self):
        mutations=[lambda s:s['boots'].pop(),lambda s:s['boots'][1].update(nonce=71),lambda s:s['boots'][0]['uart'].pop(3),lambda s:s['boots'][0]['uart'].append(s['boots'][0]['uart'][-1]),lambda s:s['boots'][0]['uart'].reverse(),lambda s:s['boots'][0]['peers'].pop('6'),lambda s:s['boots'][0]['uart'][0]['record'].update(source='ffffffffffff'),lambda s:s['boots'][0]['uart'][2]['record'].update(ticket=0),lambda s:s['boots'][0]['peer_stops'].pop(),lambda s:s['boots'][0]['peer_stops'][0].update(reaped=False),lambda s:s['boots'][0]['writes'].pop(),lambda s:s['boots'][0]['writes'][2].update(line='P 71 2 10 999\n')]
        for mutate in mutations:
            data=complete();mutate(data)
            with self.subTest(mutation=mutate),self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)
    def test_unconfirmed_loss_and_missing_resetless_recovery_are_inconclusive(self):
        for event in ['cable_disconnect_cue','cable_disconnect_receipt','host_down','cable_connect_receipt','host_up','route']:
            data=complete();data['boots'][0]['controls']=[r for r in data['boots'][0]['controls'] if r['event']!=event]
            with self.subTest(event=event),self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)
        data=complete();data['boots'][0]['uart']=[r for r in data['boots'][0]['uart'] if not(r['record']['event']=='LINK' and r['record']['link']==2)]
        with self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)
    def test_cable_loss_before_peer_request_or_after_call_cannot_pass(self):
        for offset in [-100,100]:
            data=complete()
            for row in data['boots'][0]['controls']:
                if row['event'].startswith('cable_disconnect') or row['event']=='host_down':row['monotonic']+=offset
            with self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)
    def test_extra_reset_or_unrelated_interface_cannot_pass(self):
        for row in [dict(event='reset',monotonic=.1),dict(event='host_down',interface='en0',monotonic=.1)]:
            data=complete();data['boots'][0]['controls'].append(row)
            with self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)


class ActualShape(unittest.TestCase):
    def test_concurrent_link_prefix_and_late_operator_receipt_are_accepted(self):
        data=complete();boot=data['boots'][0]
        arm=boot['uart'][1];link=dict(monotonic=arm['monotonic']+.0001,record=dict(event='LINK',token=71,seq=0,at=0,link=2,admin=1,address=1,ipv4=3232255234))
        boot['uart']=[e for e in boot['uart'] if not(e['record']['event']=='LINK' and e['record']['at']==0)]
        boot['uart'].insert(2,link)
        end=next(r['monotonic'] for r in boot['uart'] if r['record']['event']=='END' and r['record'].get('seq')==7 and not r['record']['udp'])
        receipt=next(r for r in boot['controls'] if r['event']=='cable_disconnect_receipt');receipt['monotonic']=end+.0001
        boot['controls'].sort(key=lambda r:r['monotonic'])
        self.assertEqual(m5_series.validate_series(data,SOURCE,ABI)['status'],'PASS')
    def test_batched_uart_begin_receipt_can_follow_peer_request(self):
        data=complete()
        for boot in data['boots']:
            for seq in (2,7):
                request=next(r for r in boot['peers'][str(seq)] if r['event']=='request')
                begin=next(r for r in boot['uart'] if r['record']['event']=='BEGIN' and r['record']['seq']==seq)
                begin['monotonic']=request['monotonic']+.0005
        self.assertEqual(m5_series.validate_series(data,SOURCE,ABI)['status'],'PASS')

    def test_premature_reconnect_and_up_during_udp_are_rejected(self):
        data=complete();boot=data['boots'][0]
        end=next(r for r in boot['uart'] if r['record']['event']=='END' and r['record'].get('seq')==7 and r['record']['udp'])
        up=next(r for r in boot['uart'] if r['record']['event']=='LINK' and r['record']['link']==2)
        up['record']['at']=end['record']['functional']-1
        with self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)

if __name__=='__main__':unittest.main()

class Primary(unittest.TestCase):
    def test_changed_primary_or_unknown_poststartup_state_is_rejected(self):
        for field,value in [('admin',0),('address',0),('ipv4',2130706433),('link',9)]:
            data=complete();link=next(e['record'] for e in data['boots'][0]['uart'] if e['record']['event']=='LINK');link[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):m5_series.validate_series(data,SOURCE,ABI)

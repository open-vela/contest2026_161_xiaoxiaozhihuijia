"""Explicit SIMULATION test on port 9878. Never opens serial or uses board source."""
import json
import socket
import time
import uuid
from pathlib import Path
from bridge import ticks, frequency
from test_protocol import frame

def main():
    root=Path(__file__).resolve().parent
    run='simulation_transport_'+uuid.uuid4().hex[:8]
    log=root/'evidence'/(run+'.json')
    all_acks=[]
    sent=[]
    session = 0xc0000000 | (uuid.uuid4().int & 0x0ffffffe)
    def packet(sid,eid,raw=None,clock=None):
        return dict(schema='bte_bridge_v1',source='simulation',kind='event',run_id=run,
                    raw=(raw or frame(sid,eid)).decode(),host_rx_ticks=ticks() if clock is None else clock,
                    forward_ticks=ticks(),qpc_frequency=frequency())
    def drain(sock,seconds=.3):
        deadline=time.monotonic()+seconds; buf=b''
        while time.monotonic()<deadline:
            try: buf+=sock.recv(8192)
            except socket.timeout: continue
        for line in buf.splitlines():
            try: all_acks.append(json.loads(line))
            except json.JSONDecodeError: pass
    def send(sock,m,fragmented=False):
        sent.append(m)
        raw=(json.dumps(m)+'\n').encode()
        if fragmented:
            for start in range(0,len(raw),7): sock.sendall(raw[start:start+7])
        else: sock.sendall(raw)
        drain(sock)
    with socket.create_connection(('127.0.0.1',9878),timeout=3) as sock:
        sock.settimeout(.05)
        # Accepted, duplicate, gap, new session, CRC rejection, stale rejection.
        send(sock,packet(session,1),True)
        send(sock,packet(session,1))
        send(sock,packet(session,3))
        send(sock,packet(session+1,1))
        send(sock,packet(session+1,2,raw=frame(session+1,2)[:-4]+b'0000'))
        send(sock,packet(session+1,3,clock=ticks()-5*frequency()))
    time.sleep(.5)
    with socket.create_connection(('127.0.0.1',9878),timeout=3) as sock:
        sock.settimeout(.05)
        send(sock,packet(session+1,1))  # Duplicate across TCP reconnect.
        send(sock,packet(session+1,4))
    processed=[a for a in all_acks if a.get('stage')=='processed']
    duplicates=[a for a in all_acks if a.get('stage')=='duplicate_or_out_of_order']
    ids=[(a['session'],a['event_id']) for a in processed]
    result=dict(source='simulation',run_id=run,sent=sent,acks=all_acks,
                processed=len(processed),duplicate_rejected=len(duplicates),
                unique_processed=len(set(ids)),passed=len(processed)==4 and len(duplicates)==2 and len(set(ids))==4,
                note='Software transport test only; not real hardware evidence. Check Unity log for malformed rejection.')
    log.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('sent','acks')},ensure_ascii=False,indent=2))
    return 0 if result['passed'] else 1

if __name__=='__main__': raise SystemExit(main())

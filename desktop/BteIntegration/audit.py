"""Read-only cross-stage identity audit; test and board sources stay separate."""
import argparse
import json
from collections import Counter
from pathlib import Path

def records(path):
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if not line.strip(): continue
        try: yield json.loads(line)
        except json.JSONDecodeError: continue  # In-progress final line is not evidence yet.

def identity(m):
    return m.get('source','board_serial'), m.get('session'), m.get('event_id')

def audit(bridge_path, unity_paths):
    bridge=list(records(bridge_path)); unity=[r for p in unity_paths for r in records(p)]
    run=bridge_path.parent.name
    unity=[r for r in unity if r.get('run_id')==run]
    valid=[r['message'] for r in bridge if r['stage']=='parsed' and r['message']['kind']=='event']
    forwarded=[r['message'] for r in bridge if r['stage']=='forwarded' and r['message']['kind']=='event']
    received=[r for r in unity if r['stage']=='received']
    processed=[r for r in unity if r['stage']=='processed']
    rejected=[r for r in unity if r['stage'] in ('stale_or_clock_invalid','duplicate_or_out_of_order','rejected')]
    f=Counter(map(identity,forwarded)); r=Counter(map(identity,received)); p=Counter(map(identity,processed))
    ready=[x['message'] for x in bridge if x['stage']=='parsed' and x['message']['kind']=='ready']
    latencies=[]
    for x in processed:
        if x.get('qpc_frequency',0)>0:
            latencies.append((x['unity_process_ticks']-x['host_rx_ticks'])*1000/x['qpc_frequency'])
    return dict(source='board_serial', run=run, ready=ready, valid_serial_events=len(valid),
        unique_valid_serial_events=len(set(map(identity,valid))), forwarded_events=len(forwarded),
        unity_received=len(received), unity_processed=len(processed), unity_rejected=len(rejected),
        scored_feedback=sum(x.get('outcome','').startswith(('Perfect;','Good;','Miss;')) for x in processed),
        repeated_processed=[list(k) for k,v in p.items() if v>1],
        forwarded_not_received=[list(k) for k in f.keys()-r.keys()],
        received_not_processed=[list(k) for k in r.keys()-p.keys()],
        outcomes=dict(Counter(x.get('outcome','').split(';')[0] for x in processed)),
        serial_to_unity_process_ms=dict(min=min(latencies),max=max(latencies),mean=sum(latencies)/len(latencies)) if latencies else None,
        event_records=processed,
        scope='Link transport only; no recognition accuracy, device-clock synchronization or Android BLE acceptance.')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bridge',type=Path)
    ap.add_argument('--unity',type=Path,nargs='+',required=True)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    result=audit(args.bridge,args.unity)
    text=json.dumps(result,ensure_ascii=False,indent=2)
    if args.output:
        if args.output.exists(): raise SystemExit('Refusing to overwrite existing audit evidence')
        args.output.write_text(text,encoding='utf-8')
    print(text)

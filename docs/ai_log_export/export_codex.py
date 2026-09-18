"""Transparent manual Codex adapter; uses unchanged official append_events.
Never installs hooks, changes originals, invokes network, or claims official approval.
"""
from pathlib import Path
import sys,json,re,hashlib,tarfile,collections,importlib.util,datetime
ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent
SNAP=BASE/'private_raw'/'20260918T094209Z'
OUT=ROOT/'candidate'
sys.path.insert(0,str(ROOT/'official/adapters/shared'))
spec=importlib.util.spec_from_file_location('official_core',ROOT/'official/adapters/shared/snapshot_core.py')
core=importlib.util.module_from_spec(spec);spec.loader.exec_module(core)
TEAM='contest2026_161_xiaoxiaozhihuijia';LOGIN='yangshuxuan1024'
PATTERNS={
 'private_key':re.compile(r'-----BEGIN (?:OPENSSH |RSA |EC )?PRIVATE KEY-----'),
 'token':re.compile(r'\b(?:sk-[A-Za-z0-9_-]{20,}|ghp_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{20,})'),
 'password':re.compile(r'(?:密码(?:为|是|[:：])\s*[A-Za-z0-9!@#$%^&*._-]{3,}|(?:password|passwd)\s*[=:]\s*[\x22\x27]?[A-Za-z0-9!@#$%^&*._-]{6,})',re.I),
 'bearer':re.compile(r'Bearer\s+[A-Za-z0-9._\-+/=]{16,}',re.I),
}
def digest(b):return hashlib.sha256(b).hexdigest()
def convert(obj,origin,model,cwd):
 p=obj.get('payload',{});typ=obj.get('type');ts=obj.get('timestamp')
 if not ts:raise ValueError('Missing source timestamp')
 ev={'ts':ts,'role':'system','metadata':{'manual_adapter':'codex-rollout-v1','source':origin,'raw_type':typ}}
 if cwd:ev['cwd']=cwd
 if model:ev['model']=model
 # Original payload is preserved for record-level audit, including usage counters.
 # No inferred per-message token totals are inserted; cumulative usage must not be summed.
 ev['metadata']['original_payload']=p
 if typ=='response_item' and isinstance(p,dict):
  t=p.get('type')
  if t=='message':
   role=p.get('role','system');ev['role']=role if role in {'user','assistant','system'} else 'system'
   blocks=p.get('content',[])
   if isinstance(blocks,str):ev['text']=blocks
   elif isinstance(blocks,list):
    texts=[b['text'] for b in blocks if isinstance(b,dict) and isinstance(b.get('text'),str)]
    if texts:ev['text']='\n'.join(texts)
  elif t in {'function_call','custom_tool_call'}:
   ev.update(role='tool',tool_name=p.get('name','unknown'),tool_call_id=p.get('call_id') or p.get('id') or 'source-line-'+str(origin['line']),input=p.get('arguments',p.get('input')),output=None)
   if t=='function_call' and isinstance(ev['input'],str):
    try:ev['input']=json.loads(ev['input'])
    except json.JSONDecodeError:pass
  elif t in {'function_call_output','custom_tool_call_output'}:
   ev.update(role='tool',tool_name='<result>',tool_call_id=p.get('call_id') or p.get('id') or 'source-line-'+str(origin['line']),input=None,output=p.get('output'))
  elif t=='reasoning':
   texts=[b['text'] for b in p.get('summary',[]) if isinstance(b,dict) and isinstance(b.get('text'),str)]
   if texts:ev.update(role='assistant',thinking='\n'.join(texts))
 return ev
def main():
 assert not OUT.exists(),'Use a new output directory, never append twice'
 OUT.mkdir(parents=True)
 local=json.loads((SNAP/'local_manifest.json').read_text(encoding='utf8'))
 remote=json.loads((SNAP/'remote_manifest.json').read_text(encoding='utf8'))
 groups=collections.defaultdict(list);inputs=[]
 def load(row,data,host):
  assert digest(data)==row['sha256'],'Original hash mismatch'
  groups[(host,row['session_id'])].append((row,data))
  inputs.append({'host':host,'session_id':row['session_id'],'file':row['archive_path'],'sha256':row['sha256']})
 for row in local['files']:load(row,(SNAP/row['archive_path']).read_bytes(),'local')
 with tarfile.open(SNAP/remote['archive_file'],'r:gz') as tf:
  for row in remote['files']:load(row,tf.extractfile(row['archive_path']).read(),'server')
 prior_flags=json.loads((BASE/'reports'/SNAP.name/'sensitive_locations_private.json').read_text(encoding='utf8'))
 flagged={(x['host'],x['session_id']) for x in prior_flags}
 member=OUT/'logs'/LOGIN;member.mkdir(parents=True)
 manifest={'schema_version':'1.0','team_id':TEAM,'github_login':LOGIN,'generator':'codex-manual-adapter@1 (official append_events unchanged)','updated_at':core.iso_now(),'sessions':[]}
 decisions=[];total=0
 for (host,sid),sources in sorted(groups.items()):
  reasons=set();records=[];previous_file_records=set();duplicate_count=0
  if (host,sid) in flagged:reasons.add('prior_sensitive_flag')
  for row,data in sources:
   if isinstance(row.get('source'),dict):reasons.add('subagent_separate')
   this_file=set()
   for line,raw in enumerate(data.splitlines(),1):
    if not raw.strip():continue
    try:obj=json.loads(raw)
    except Exception:reasons.add('invalid_json');continue
    text=json.dumps(obj,ensure_ascii=False)
    for label,pattern in PATTERNS.items():
     if pattern.search(text):reasons.add('sensitive_'+label)
    key=digest(json.dumps(obj,sort_keys=True,ensure_ascii=False).encode())
    this_file.add(key)
    if key in previous_file_records:duplicate_count+=1;continue
    records.append((obj,{'sha256':row['sha256'],'line':line,'record_sha256':digest(raw)},row))
   previous_file_records.update(this_file)
  decision={'host':host,'session_id':sid,'source_files':len(sources),'duplicate_records_across_files':duplicate_count}
  if reasons:
   decisions.append({**decision,'status':'excluded_whole_session','reasons':sorted(reasons)});continue
  records.sort(key=lambda x:x[0].get('timestamp',''))
  events=[];model=None;cwd=sources[0][0].get('cwd');bad=False
  for obj,origin,row in records:
   p=obj.get('payload',{})
   if obj.get('type')=='turn_context' and isinstance(p,dict):model=p.get('model',model);cwd=p.get('cwd',cwd)
   if obj.get('type')=='session_meta' and isinstance(p,dict) and p.get('id')!=sid:bad=True
   try:events.append(convert(obj,origin,model,cwd))
   except ValueError:bad=True
  if bad or not events:
   decisions.append({**decision,'status':'excluded_whole_session','reasons':['missing_timestamp_or_session_mismatch_or_empty']});continue
  rel=f'logs/{LOGIN}/{events[0]["ts"][:10]}/codex__{sid}.jsonl';dest=OUT/rel
  assert not dest.exists(),'Duplicate session ID across hosts needs manual review'
  # Sensitive sessions have already been excluded. No redaction of accepted source content.
  result=core.append_events(dest,events,sid,TEAM,LOGIN,'codex',0,[])
  mode='cli' if host=='server' else 'vscode_extension_partial'
  warning='Manual historical Codex rollout adaptation; snapshot only, not live official hook capture. Original payload and source hashes retained. No official acceptance claimed.'
  if host=='local':warning+=' Desktop uses closest schema collection_mode; not a VS Code provenance claim. Original cwd is retained; .repo gate was not fabricated.'
  manifest['sessions'].append({'session_id':sid,'tool':'codex','started_at':events[0]['ts'],'last_event_at':events[-1]['ts'],'event_count':result['written'],'file_path':rel,'collection_mode':mode,'data_completeness_warning':warning,'health':'degraded','manual_export':True,'source_host':host,'original_capture_source':sources[0][0].get('source'),'source_hashes':[x[0]['sha256'] for x in sources]})
  decisions.append({**decision,'status':'exported','events':len(events),'sha256':digest(dest.read_bytes()),'file_path':rel});total+=len(events)
 (member/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 report={'snapshot':SNAP.name,'raw_files':len(inputs),'sessions_considered':len(groups),'sessions_exported':len(manifest['sessions']),'events_exported':total,'decisions':decisions,'original_files':inputs,'adapter_sha256':digest(Path(__file__).read_bytes()),'originals_modified':False,'github_modified':False,'coverage':'Earlier two-host selected snapshot only; not all project sessions; subsequent development and other hardware task snapshots not included.'}
 (ROOT/'private_export_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
 print(json.dumps({k:v for k,v in report.items() if k not in {'decisions','original_files'}},ensure_ascii=False,indent=2))
if __name__=='__main__':main()

from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'SHA256SUMS.json').read_text(encoding='utf-8'))
bad=[]
for item in manifest['files']:
 p=(root/item['path']).resolve()
 if not p.is_relative_to(root) or not p.is_file():bad.append(item['path']);continue
 if hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:bad.append(item['path'])
if bad:
 print(json.dumps({'status':'FAIL','files':bad},ensure_ascii=False));sys.exit(1)
print(json.dumps({'status':'PASS','checked_files':len(manifest['files'])}))

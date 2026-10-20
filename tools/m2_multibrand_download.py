import json,re,urllib.request,urllib.parse,hashlib,concurrent.futures
from pathlib import Path
root=Path('research/m2/multibrand');rows=json.loads((root/'repository.json').read_text())['data']
ids=['1582','3221','1351','1307','1230','2102','5227','3381','3098','7008','7317','5424']
def download(id):
 r=next(r for r in rows if f'getfile.php/{id}/' in r[7]);url=re.search("href='([^']+)'",r[7])[1];ext=url.rsplit('.',1)[1];sha=re.search('[a-f0-9]{64}',r[7])[0];target=root/(id+'.'+ext)
 if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=sha:
  u=urllib.parse.quote(url,safe=':/');data=urllib.request.urlopen(u,timeout=90).read();assert hashlib.sha256(data).hexdigest()==sha;target.write_bytes(data)
 item=dict(id=id,make=r[0],model=r[1],mode=r[2],url=url,license='CC0-1.0',sha256=sha,path=str(target.resolve()),bytes=target.stat().st_size)
 print('downloaded',id,r[1],target.stat().st_size,flush=True);return item
out=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
 for f in concurrent.futures.as_completed([ex.submit(download,i) for i in ids]):
  try:out.append(f.result())
  except Exception as e:print('DOWNLOAD ERROR',repr(e),flush=True)
(root/'manifest.json').write_text(json.dumps(out,indent=2),encoding='utf-8')

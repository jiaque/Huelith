"""Download a bounded, reproducible set of public NASA portrait validation fixtures."""
import concurrent.futures,hashlib,json
from pathlib import Path
from urllib.request import Request,urlopen
from urllib.parse import urlencode
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'research/m3/portraits';OUT.mkdir(parents=True,exist_ok=True)
NAMES=['Mae Jemison','Bernard Harris','Victor Glover','Leroy Chiao','Sunita Williams','Ellen Ochoa','Kalpana Chawla','Chris Hadfield','Jessica Watkins','Christina Koch','Woody Hoburg','Frank Rubio','Jasmin Moghbeli','Koichi Wakata','Akihiko Hoshide','Mark Vande Hei','John Glenn']
def get(url):
 with urlopen(Request(url,headers={'User-Agent':'Huelith-portrait-validation'}),timeout=60) as response:return response.read()
def fetch(name):
 path=OUT/(name.lower().replace(' ','-')+'.jpg');record=path.with_suffix('.json')
 if path.exists() and record.exists():return json.loads(record.read_text())
 data=json.loads(get('https://images-api.nasa.gov/search?'+urlencode(dict(q=name+' portrait',media_type='image',page_size=5))))
 items=data['collection']['items'];item=next((x for x in items if 'portrait' in x['data'][0].get('title','').lower()),items[0])
 files=json.loads(get(item['href']));url=next((x for x in files if x.endswith('~large.jpg')),next((x for x in files if x.endswith('~orig.jpg')),item['links'][0]['href']))
 content=get(url);path.write_bytes(content)
 info=dict(query=name,source=item['href'],url=url,title=item['data'][0]['title'],nasa_id=item['data'][0]['nasa_id'],sha256=hashlib.sha256(content).hexdigest(),path=str(path.relative_to(ROOT)),usage='Local software validation; source attribution retained')
 record.write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8');return info
if __name__=='__main__':
 records=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
  for name,future in zip(NAMES,[pool.submit(fetch,name) for name in NAMES]):
   try:records.append(future.result());print(name,'OK',flush=True)
   except Exception as error:print(name,str(error),flush=True)
 (OUT/'manifest.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')

"""Compare prototype container strategies using deterministic floating-point tiles."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import time
import zipfile
from contextlib import closing

import numpy as np

parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
random=np.random.default_rng(123)
tiles=[random.random((512,512,4),dtype=np.float32).tobytes() for _ in range(64)]
hashes=[hashlib.sha256(t).hexdigest() for t in tiles]
report={'payload_bytes':sum(map(len,tiles)),'tiles':len(tiles),'tile_bytes':len(tiles[0]),'results':[]}
with tempfile.TemporaryDirectory(prefix='m0-containers-',dir=args.output.parent) as temporary:
    folder=Path(temporary)
    for name,compression in [('zip64-stored',zipfile.ZIP_STORED),('zip64-deflate1',zipfile.ZIP_DEFLATED)]:
        path=folder/(name+'.zip');start=time.perf_counter()
        with zipfile.ZipFile(path,'w',compression=compression,compresslevel=1,allowZip64=True) as archive:
            archive.writestr('manifest.json',json.dumps(hashes))
            for i,tile in enumerate(tiles):
                with archive.open(str(i),'w',force_zip64=True) as entry: entry.write(tile)
        write_seconds=time.perf_counter()-start;start=time.perf_counter()
        with zipfile.ZipFile(path) as archive:
            for i in [63,0,31,7,55,2,42,16]:assert hashlib.sha256(archive.read(str(i))).hexdigest()==hashes[i]
        report['results'].append(dict(container=name,bytes=path.stat().st_size,write_seconds=write_seconds,
            eight_random_reads_seconds=time.perf_counter()-start,hashes_match=True))
    path=folder/'tiles.sqlite';start=time.perf_counter()
    with closing(sqlite3.connect(path)) as db:
        db.execute('CREATE TABLE tiles (id INTEGER PRIMARY KEY, data BLOB NOT NULL)')
        db.executemany('INSERT INTO tiles VALUES (?,?)',enumerate(tiles));db.commit()
        write_seconds=time.perf_counter()-start;start=time.perf_counter()
        for i in [63,0,31,7,55,2,42,16]:assert hashlib.sha256(db.execute('SELECT data FROM tiles WHERE id=?',(i,)).fetchone()[0]).hexdigest()==hashes[i]
        read_seconds=time.perf_counter()-start
        db.execute('BEGIN');db.execute('UPDATE tiles SET data=? WHERE id=0',(b'partial failure',));db.rollback()
        rollback=hashlib.sha256(db.execute('SELECT data FROM tiles WHERE id=0').fetchone()[0]).hexdigest()==hashes[0]
    report['results'].append(dict(container='sqlite',bytes=path.stat().st_size,write_seconds=write_seconds,
        eight_random_reads_seconds=read_seconds,hashes_match=True,rollback_preserves_old=rollback))
report['limitations']=['256MiB local warm-cache experiment; not a >4GiB or power-failure qualification.',
    'Random float data, not representative photo compression ratio.',
    'ZIP64 forced records tested; not enough evidence to freeze final project format.']
args.output.write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report),flush=True)

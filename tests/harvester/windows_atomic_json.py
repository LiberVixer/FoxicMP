"""Windows sharing regression: JSON readers must permit atomic replacement."""
import json
import sys
import tempfile
import threading
import time
from pathlib import Path
root=Path(sys.argv[1]);sys.path.insert(0,str(root))
from scripts.progressive_session import atomic_json,read_json
with tempfile.TemporaryDirectory(dir=root) as folder:
    path=Path(folder)/'session.json';atomic_json(path,{'n':0})
    halt=threading.Event();errors=[];observed=[0,0]
    def read(index):
        previous=0
        try:
            while not halt.is_set():
                # One legacy CRT reader briefly denies DELETE, as older clients/scanners do.
                if index:
                    try:
                        with path.open('rb') as file:
                            data=file.read();time.sleep(.003)
                    except PermissionError:
                        time.sleep(.003);continue
                    n=json.loads(data)['n'];time.sleep(.003)
                else:n=read_json(path)['n']
                assert n>=previous,(previous,n)
                previous=n;observed[index]+=1
        except BaseException as error:errors.append(str(error))
    threads=[threading.Thread(target=read,args=(i,)) for i in range(2)]
    for thread in threads:thread.start()
    try:
        for n in range(1,1001):atomic_json(path,{'n':n})
    finally:
        halt.set()
        for thread in threads:thread.join(5)
    assert not errors,errors
    assert all(observed),observed
    assert read_json(path)['n']==1000
    assert not list(Path(folder).glob('*.new-*'))
    (root/'atomic-json-result.json').write_text(json.dumps({'passed':True,'updates':1000,'reader_observations':observed}))

"""Structural EBML budget failures against the production sequential reader."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
from progressive_webm_fixtures import element,width
from progressive_webm_remux_preflight import scan

def encoded(tag,payload):
    n=max(1,(len(payload).bit_length()+6)//7)
    if len(payload)==(1<<(7*n))-1:n+=1
    return tag.to_bytes((tag.bit_length()+7)//8,'big')+((1<<(7*n))|len(payload)).to_bytes(n,'big')+payload

def run(runner,source,report):
    raw=Path(source).read_bytes();layout=scan(raw);cluster=layout['clusters'][0];block=layout['blocks'][0];head=raw[:cluster['offset']]
    _,_,ebml_end=element(raw,0);_,seg_begin,_=element(raw,ebml_end)
    first=raw[block['offset']:block['end']];clock=encoded(0xe7,b'\0');unknown=bytes.fromhex('1f43b675ff')
    cases=[]
    def add(name,data,code,count=600):cases.append((name,data,code,count))
    add('finite-segment',raw[:ebml_end]+encoded(0x18538067,raw[seg_begin:]),'unsupported-stream-webm-segment')
    bad=bytearray(raw);bad[block['offset']]=0xa0;add('blockgroup',bad,'unsupported-stream-webm-block')
    bad=bytearray(raw);at=block['offset']+1;n=width(bad[at]);bad[at:at+n]=((1<<(7*n+1))-1).to_bytes(n,'big');add('unknown-block',bad,'stream-webm-cluster-limit')
    add('duplicate-clock',head+unknown+clock+first+clock,'unsupported-stream-webm-clock')
    nested=encoded(0x45a3,b'name')
    for _ in range(9):nested=encoded(0x67c8,nested)
    add('metadata-depth',head+encoded(0x1254c367,nested)+raw[cluster['offset']:],'stream-webm-depth-limit')
    add('head-bytes',head+encoded(0xec,bytes(1048576))+raw[cluster['offset']:],'stream-webm-head-limit')
    add('cluster-bytes',head+unknown+clock+first+encoded(0xec,bytes(16777216)),'stream-webm-cluster-limit')
    add('cluster-children',head+unknown+clock+first+encoded(0xec,b'')*65536,'stream-webm-element-limit')
    add('finite-cluster-boundary',head+encoded(0x1f43b675,clock+first[:-1])+first[-1:],'stream-webm-cluster-limit')
    add('zero-sample-count',raw,'unsupported-stream-webm-source',0)
    add('sample-count-budget',raw,'unsupported-stream-webm-source',50001)
    rows=[]
    with tempfile.TemporaryDirectory(prefix='fox-webm-limits-') as tmp:
        path=Path(tmp)/'input.webm'
        for name,data,code,count in cases:
            path.write_bytes(data);r=subprocess.run([str(runner),str(path),str(count)],capture_output=True,timeout=90)
            assert r.returncode==1 and r.stderr.decode().strip()==code,(name,r.returncode,r.stderr)
            rows.append(dict(profile=name,exitCode=r.returncode,errorCode=code,inputSha256=hashlib.sha256(data).hexdigest()))
    value=dict(result='PASS',count=len(rows),rows=rows,runnerSha256=hashlib.sha256(Path(runner).read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    Path(report).write_text(json.dumps(value,indent=2)+'\n');return value

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--source',required=True);p.add_argument('--report',required=True);a=p.parse_args();print(json.dumps(run(Path(a.runner).resolve(),a.source,a.report)))

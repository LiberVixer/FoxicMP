"""Generate an unshipped Classic source variant to reproduce the index/EOF race.

Compile the result instead of src/DSUtil/HarvesterSession.cpp, adding /I src/DSUtil.
FOXICMP_TEST_CONTINUOUS_INDEX_DELAY=800 delays noninitial index refreshes;
FOXICMP_TEST_CANSEEK_TRACE names a private JSONL decision log. Use isolated
Classic copies and the ordinary progressive_aac_package_test.py driver.
The ordinary source and installed applications are never modified by this tool.
"""
import argparse
import hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def instrument(source):
    point='    m_continuousReader->refresh();auto next=Snapshot();bool same=next.revision!=0;'
    assert source.count(point)==1, 'Continuous refresh injection point changed'
    source=source.replace(point,'''    if(const char* delay=std::getenv("FOXICMP_TEST_CONTINUOUS_INDEX_DELAY")) {
        const auto old=Snapshot();if(old.revision && output.visible>old.tracks[0].available)Sleep(DWORD(std::strtoul(delay,nullptr,10)));
    }
'''+point).replace('#include "../../shared/','#include "../shared/').replace('#include <map>','#include <map>\n#include <fstream>\n#include <cstdlib>')
    old='    return Complete() || position<ReadyEnd();'
    assert source.count(old)==1, 'Classic seek guard changed'
    new='        if(output.complete)return true;'
    if new not in source:
        source=source.replace(old,'''    const bool complete=Complete();const auto ready=ReadyEnd();const bool allowed=complete || position<ready;
    if(!allowed)if(const char* path=std::getenv("FOXICMP_TEST_CANSEEK_TRACE")) {
        const auto output=m_continuousSource?m_continuousSource->status():foxicmp::progressive::StreamCacheStatus{};
        std::ofstream log(path,std::ios::app);log<<"{\\"allowed\\":false,\\"position\\":"<<position<<",\\"readyEnd\\":"<<ready<<",\\"bridgeComplete\\":"<<(complete?"true":"false")<<",\\"outputComplete\\":"<<(output.complete?"true":"false")<<"}\\n";
    }return allowed;''')
    if new in source:
        assert source.count(new)==1
        source=source.replace(new,'''        if(output.complete){if(const char* path=std::getenv("FOXICMP_TEST_CANSEEK_TRACE")) {
            std::ofstream log(path,std::ios::app);log<<"{\\"allowed\\":true,\\"position\\":"<<position<<",\\"readyEnd\\":"<<ReadyEnd()<<",\\"bridgeComplete\\":"<<(Complete()?"true":"false")<<",\\"outputComplete\\":true}\\n";
        }return true;}''')
    return source


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=ROOT/'src/DSUtil/HarvesterSession.cpp')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();target=args.output.resolve()
    assert target.parent==ROOT/'_bin', 'Generate only into this checkout private _bin directory'
    assert not target.exists(), 'Refusing to replace an existing artifact'
    text=instrument(args.source.read_text(encoding='utf-8'))
    with target.open('x',encoding='utf-8',newline='\n') as output:output.write(text)
    print(target.name,hashlib.sha256(target.read_bytes()).hexdigest())


if __name__=='__main__':main()

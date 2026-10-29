"""Actual applied metadata, chapter actions, clear and same-process new session."""
import argparse,copy,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient,ApiError
from progressive_v2 import V2PreviewSession
from progressive_multitrack_conformance import fixture
from progressive_v2_playback_conformance import forget_closed_readers

REQUEST_TITLE='Запрос проигрывания'
def exercise(client,fixtures,work,select_chapter=None,check_title=None):
    root=Path(work);root.mkdir(parents=True)
    report={};preview=None
    def prepare(name,title):
        folder=root/name;value,data=fixture(fixtures,folder)
        value['tracks']=value['tracks'][:2];value['resources']=value['resources'][:2]
        value['metadata']=dict(revision='1',title=title,chapters=[dict(id='intro',time='0',title='Введение'),dict(id='main',time='40000000',title='Часть 1')])
        return folder,value,data
    def wait_applied(revision,title,count,seconds=0):
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            state=preview.heartbeat();(root/'last-status.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
            assert not state.get('error') and state.get('state')!='error',state
            applied=state.get('appliedMetadata',{})
            if applied.get('revision')==revision and applied.get('title')==title and len(applied.get('chapters',[]))==count and state.get('positionSeconds',0)>=seconds:
                assert state['readerId']==preview.reader_id and not state.get('released'),state
                if check_title:check_title(title)
                time.sleep(.2)
                return state
            time.sleep(.05)
        raise AssertionError(('Actual UI metadata not applied',revision,title,count,state))
    def publish_meta(metadata):
        value['metadata']=metadata;value['revision']=str(int(value['revision'])+1);preview.publish(value)
    try:
        folder,value,data=prepare('first','Первый {app}')
        preview=V2PreviewSession(client,str(folder),title=REQUEST_TITLE);preview.publish(value)
        report['initial']=wait_applied('1','Первый {app}',2,1)
        publish_meta(dict(revision='2',title='Позднее название',chapters=[dict(id='intro',time='0',title='Введение'),dict(id='chosen',time='40000000',title='Выбор главы'),dict(id='future',time='160000000',title='Будущая глава')]))
        report['late']=wait_applied('2','Позднее название',3,1.5)
        if select_chapter:select_chapter(1)
        report['chapterNavigation']=wait_applied('2','Позднее название',3,4)
        # Both wire entrypoints reject invalid metadata without changing source or UI.
        for edit in ('same-revision','outside-duration'):
            bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1)
            if edit=='same-revision':bad['metadata']['title']='Rejected'
            else:bad['metadata']['revision']='3';bad['metadata']['chapters'][-1]['time']='210000000'
            try:client.update(bad,reader_id=preview.reader_id)
            except ApiError:pass
            else:raise AssertionError(('Invalid metadata accepted',edit))
            report[edit]=wait_applied('2','Позднее название',3)
        publish_meta(dict(revision='3',title='Очищено',chapters=[]))
        report['cleared']=wait_applied('3','Очищено',0)
        publish_meta(dict(revision='4'))
        report['omitted']=wait_applied('4',REQUEST_TITLE,0)
        publish_meta(dict(revision='5',title='Восстановлено',chapters=[dict(id='restored',time='0',title='Возвращённая глава')]))
        report['restored']=wait_applied('5','Восстановлено',1)
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        folder,value,data=prepare('second','Второй сеанс')
        preview=V2PreviewSession(client,str(folder),title=REQUEST_TITLE);preview.publish(value)
        report['secondSession']=wait_applied('1','Второй сеанс',2,1)
        assert report['initial']['sessionId']!=report['secondSession']['sessionId']
        assert report['initial']['readerId']!=preview.reader_id
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        report['result']='PASS';return report
    finally:
        if preview and not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);a=p.parse_args()
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work),ensure_ascii=False))

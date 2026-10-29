"""Real multi-audio/late complete SRT while all A/V resources are incomplete."""
import argparse,json,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_v1_conformance import fragment_prefix
from progressive_webm_fixtures import webm_prefix
from progressive_v2_playback_conformance import forget_closed_readers

def fixture(fixtures,work):
    root=Path(work);root.mkdir(parents=True)
    sources=['progressive/video.mp4','progressive/audio.m4a','progressive-webm-vp9-8/audio.webm','progressive-webm-vorbis-2/audio.webm']
    codecs=['h264','aac-lc','opus','vorbis'];containers=['fmp4','fmp4','webm','webm'];labels=['Video','Русский AAC','English Opus','Commentary Vorbis'];languages=['','rus','eng','deu']
    data=[(Path(fixtures)/name).read_bytes() for name in sources]
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),metadata=dict(revision='1',title='Progressive test',chapters=[dict(id='intro',time='0',title='Введение'),dict(id='main',time='50000000',title='Основная часть')]),resources=[],tracks=[])
    for index,content in enumerate(data):
        count=(fragment_prefix if index<2 else webm_prefix)(content,12);name='track-'+str(index)+'.part';(root/name).write_bytes(content[:count]);rid='res-'+str(index)
        value['resources'].append(dict(id=rid,name=name,length=None,ranges=[dict(offset='0',length=str(count))],state='downloading'))
        value['tracks'].append(dict(id='track-'+str(index),kind='audio' if index else 'video',required=True,codecHint=codecs[index],containerHint=containers[index],initializationReady=True,state='downloading',source=dict(resourceId=rid),label=labels[index],language=languages[index],default=index==1))
    for index in range(2):
        rid='sub-'+str(index);name=rid+'.part';text=('1\n00:00:00,000 --> 00:00:20,000\n'+('Ранние subtitles' if index==0 else 'Поздние subtitles')+'\n\n').encode()
        if index==0:(root/name).write_bytes(text)
        value['resources'].append(dict(id=rid,name=name,length=str(len(text)) if index==0 else None,ranges=[dict(offset='0',length=str(len(text)))] if index==0 else [],state='complete' if index==0 else 'downloading'))
        value['tracks'].append(dict(id=rid,kind='subtitle',required=False,codecHint='srt',containerHint='srt',initializationReady=index==0,state='complete' if index==0 else 'downloading',source=dict(resourceId=rid),label='Early Russian' if index==0 else 'Late Russian',language='rus',default=index==0))
        data.append(text)
    return value,data

def exercise(client,fixtures,work,select_audio=None,select_subtitle=None,seek=None):
    root=Path(work);value,data=fixture(fixtures,root)
    caps=client.request(dict(version=1,action='capabilities'));assert caps['schema2Playback']['multiAudio'] and caps['schema2Playback']['subtitles']['lateCompletion'],caps
    assert client.probe(value,root)['supported']
    preview=V2PreviewSession(client,str(root),title='Multi audio and late SRT');report={};late=False;stage=0;backward=False;audio_indices=set()
    try:
        preview.publish(value);deadline=time.monotonic()+50
        while time.monotonic()<deadline:
            state=preview.heartbeat();(root/'last-status.json').write_text(json.dumps(dict(stage=stage,status=state),ensure_ascii=False,indent=2),encoding='utf-8');assert state.get('state')!='error',state;position=state.get('positionSeconds',0)
            selection=state.get('audioSelection',{});
            if selection.get('index',-1)>=0:audio_indices.add(selection['index'])
            if position>=3 and not late:
                r=value['resources'][5];(root/r['name']).write_bytes(data[5]);r.update(length=str(len(data[5])),ranges=[dict(offset='0',length=str(len(data[5])))],state='complete');value['tracks'][5].update(initializationReady=True,state='complete');value['revision']=str(int(value['revision'])+1);preview.publish(value);late=True
            if select_audio:
                if stage==0 and position>=2:select_audio(1);stage=1
                elif stage==1 and position>=4:select_audio(1);stage=2
                elif stage==2 and position>=6:select_audio(1);stage=3
                elif stage==3 and position>=8 and seek:seek(2);stage=4
                if stage==4 and position<7:backward=True
            if late and select_subtitle and stage==2:select_subtitle();select_subtitle=None
            if position>=10 and late and (not select_audio or backward):
                assert all((root/value['resources'][i]['name']).stat().st_size<len(data[i]) for i in range(4));tracks=state['mediaTracks'];assert len(tracks)==4 and all(int(t['decoderIoBytes'])>0 for t in tracks),state
                assert len(state['subtitleTracks'])==2 and all(t['ready'] for t in state['subtitleTracks']),state
                report['playingWhileIncomplete']=state;report['backwardSeek']=backward;report['audioIndicesSeen']=sorted(audio_indices)
                if select_audio:assert len(audio_indices)==3 and state['audioSelection']['count']==3 and state['loadedSubtitleCount']>=2,state
                break
            time.sleep(.1)
        else:raise AssertionError(('No multi-track incomplete playback',json.dumps(state,ensure_ascii=False)))
        for index in range(4):
            r=value['resources'][index];path=root/r['name'];path.rename(path.with_suffix('.done'));path=path.with_suffix('.done')
            with path.open('ab') as stream:stream.write(data[index][stream.tell():])
            r.update(length=str(len(data[index])),ranges=[dict(offset='0',length=str(len(data[index])))],state='complete');value['tracks'][index]['state']='complete'
        value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';preview.publish(value);deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            state=preview.status();assert state.get('state')!='error',state
            if state.get('state')=='finished' and state.get('released'):report['terminal']=state;break
            time.sleep(.1)
        else:raise AssertionError(('No natural multi-track finish',state))
        assert preview.close(timeout=5)['released'];forget_closed_readers(client,value)
        report['result']='PASS';return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);args=parser.parse_args()
    print(json.dumps(exercise(ProgressiveClient(args.player,args.profile),args.fixtures,args.work)))

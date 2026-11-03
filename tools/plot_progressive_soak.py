"""Render recorded soak metrics; matplotlib is a documentation-only dependency."""
import argparse
import json
from pathlib import Path
import tempfile
import os


def _render(report, destination):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    data=json.loads(Path(report).read_text(encoding='utf-8'))
    fig,axes=plt.subplots(3,2,figsize=(10,8.4),layout='constrained')
    colors=('#d87926','#34834d')
    for row,(key,title) in enumerate((('linuxQt','Linux Qt'),('windowsQt','Windows Qt'),('classic','Windows Classic'))):
        result=data['editions'][key]['result'];samples=result['samples'];minutes=[s['seconds']/60 for s in samples]
        for peer,color in enumerate(colors):
            label='PCM16 / prefix' if peer==0 else 'PCM24 / sparse hint'
            axes[row,0].plot(minutes,[s['peers'][peer]['metrics']['privateBytes']/2**20 for s in samples],label=label,color=color,lw=1.5)
            axes[row,1].plot(minutes,[s['peers'][peer]['metrics']['handles'] for s in samples],label=label,color=color,lw=1.5)
        for ax in axes[row]:
            for pause in result['pauses']:
                start=pause['startedSeconds']/60;end=start+pause['actualSeconds']/60;ax.axvspan(start,end,color='#999999',alpha=.16,lw=0)
            ax.grid(alpha=.22);ax.set_xlim(0,10);ax.set_xlabel('Elapsed time, min');ax.spines[['top','right']].set_visible(False)
        axes[row,0].set_title(title+' — '+('RssAnon' if key=='linuxQt' else 'PrivateUsage'))
        axes[row,0].set_ylabel('MiB');axes[row,1].set_title(title+' — '+('file descriptors' if key=='linuxQt' else 'handles'))
        axes[row,1].set_ylabel('Count')
    axes[0,1].legend(frameon=False,fontsize=8)
    fig.suptitle('FoxicMP — 600 s growing RF64 soak\nShaded: writer pauses 10 / 30 / 60 s; different OS memory metrics are not directly comparable',fontsize=11)
    destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True);fig.savefig(destination,dpi=150);plt.close(fig)

def render(report,destination):
    previous=os.environ.get('MPLCONFIGDIR')
    with tempfile.TemporaryDirectory(prefix='foxicmp-soak-plot-') as directory:
        os.environ['MPLCONFIGDIR']=directory
        try:_render(report,destination)
        finally:
            if previous is None:os.environ.pop('MPLCONFIGDIR',None)
            else:os.environ['MPLCONFIGDIR']=previous

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('report');p.add_argument('destination');a=p.parse_args();render(a.report,a.destination)

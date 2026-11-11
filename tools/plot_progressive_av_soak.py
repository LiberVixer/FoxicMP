"""Plot recorded main-process resources from the three-job A/V soak."""
import argparse
import json
import os
from pathlib import Path
import tempfile

def render(report,destination):
    previous=os.environ.get('MPLCONFIGDIR')
    with tempfile.TemporaryDirectory(prefix='foxicmp-av-soak-plot-') as directory:
        os.environ['MPLCONFIGDIR']=directory
        try:
            import matplotlib
            matplotlib.use('Agg')
            import matplotlib.pyplot as plt
            data=json.loads(Path(report).read_text());fig,axes=plt.subplots(3,4,figsize=(15,8.5),layout='constrained')
            for row,edition in enumerate(data['editions']):
                result=edition['evidence'];samples=result['samples'];minutes=[s['seconds']/60 for s in samples]
                title=edition['label']
                for peer,color in enumerate(['#db7b28','#34834d']):
                    metrics=[s['mainMetrics'][peer] for s in samples]
                    cpu=[float('nan')]+[(b['cpuSeconds']-a['cpuSeconds'])/(samples[i]['seconds']-samples[i-1]['seconds']) for i,(a,b) in enumerate(zip(metrics,metrics[1:]),1)]
                    for col,values in enumerate([[m['privateBytes']/2**20 for m in metrics],[m['handles'] for m in metrics],[m['threads'] for m in metrics],cpu]):
                        axes[row,col].plot(minutes,values,color=color,lw=1.2,label='Viewer '+str(peer+1))
                for col,name in enumerate(['Private metric, MiB','FDs' if edition['platform']=='linux' else 'Handles','Threads','CPU, logical cores']):
                    ax=axes[row,col];ax.set_title(title+' / '+name,fontsize=9);ax.grid(alpha=.2);ax.set_xlabel('Elapsed minutes');ax.spines[['top','right']].set_visible(False)
                    for event in result['events']:
                        if event['event']=='cycle-started':ax.axvline(event['seconds']/60,color='#555555',alpha=.3,ls=':')
            axes[0,0].legend(frameon=False,fontsize=8)
            fig.suptitle('FoxicMP: three complete 600 s growing H.264/AAC jobs in the same two main processes\nRssAnon on Linux / PrivateUsage on Windows; dotted lines mark new jobs',fontsize=11)
            destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True);fig.savefig(destination,dpi=150);plt.close(fig)
        finally:
            if previous is None:os.environ.pop('MPLCONFIGDIR',None)
            else:os.environ['MPLCONFIGDIR']=previous

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('report');parser.add_argument('destination');args=parser.parse_args();render(args.report,args.destination)

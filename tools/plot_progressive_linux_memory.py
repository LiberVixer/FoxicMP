"""Plot the original and fixed uninstrumented Linux A/V soak results."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def plot(before, after, output):
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True, constrained_layout=True)
    for name, result, color, style in [('До исправления', before, '#b64e61', '--'),
                                        ('После исправления', after, '#25886b', '-')]:
        assert result['result'] == 'PASS' and result['soakAcceptance']
        rows = result['samples']
        for peer in range(2):
            x = [row['seconds'] / 60 for row in rows]
            for ax, key, title in zip(axes, ['privateBytes', 'rssBytes'], ['Private: RssAnon', 'RSS: VmRSS']):
                y = [row['mainMetrics'][peer][key] / 1024**2 for row in rows]
                ax.plot(x, y, color=color, linestyle=style, alpha=1 if peer == 0 else .55,
                        linewidth=1.7, label=f'{name}, экземпляр {peer + 1}')
                ax.set_ylabel(title + ', MiB')
                ax.grid(alpha=.2)
    axes[0].legend(fontsize=9, ncol=2)
    axes[0].set_title('Linux Qt: три полных A/V-файла в тех же двух процессах')
    axes[-1].set_xlabel('Реальное время, минуты')
    fig.savefig(output, dpi=150)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    plot(json.loads(args.before.read_text()), json.loads(args.after.read_text()), args.output)

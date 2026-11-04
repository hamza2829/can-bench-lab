import argparse
from pathlib import Path
import sys

from .analysis import write_analysis
from .logs import read_log
from .signals import load_description


def main(argv=None):
    parser = argparse.ArgumentParser(description='Analyze timestamped classic CAN candump logs')
    parser.add_argument('log', type=Path)
    parser.add_argument('--description', type=Path, default=Path('examples/lifter.yaml'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--signal', default='position')
    parser.add_argument('--bitrate', type=int, default=500000)
    args = parser.parse_args(argv)
    try:
        result = write_analysis(read_log(args.log), load_description(args.description), args.output, args.signal, args.bitrate)
        print(f'{len(result["findings"])} findings; report and plot in {args.output}')
        return 0
    except (ValueError, OSError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

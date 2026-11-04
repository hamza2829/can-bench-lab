from pathlib import Path
import subprocess

for log, output in [(Path('examples/faults.log'), Path('artifacts/fault-analysis')),
                    (Path('artifacts/bench/test_motion_and_tpdo_mapping.memory.log'), Path('artifacts/bench-analysis'))]:
    subprocess.run(['canbench', str(log), '--description', 'examples/lifter.yaml', '--output', str(output)], check=True)

tracked = subprocess.run(['git', 'ls-files', '-z'], capture_output=True, text=True, check=True).stdout.split('\x00')
for name in filter(None, tracked):
    path = Path(name)
    if path.is_file():
        assert chr(0x2014) not in path.read_text(encoding='utf-8'), path

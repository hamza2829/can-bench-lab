"""Measure unique Unity cases, pytest invocations, and real coverage counters."""
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET

def unity_counts(path):
    matches = re.findall(r'(\d+) Tests (\d+) Failures (\d+) Ignored', path.read_text(encoding='utf-8'))
    if len(matches) != 1:
        raise SystemExit(f'Cannot find a unique Unity summary in {path}')
    return tuple(map(int, matches[0]))

c_tests, c_failures, c_ignored = unity_counts(Path('artifacts/ctest.log'))
asan_tests, asan_failures, asan_ignored = unity_counts(Path('artifacts/sanitizers.log'))
suites = ET.parse('artifacts/pytest.xml').getroot()
counts = {key: sum(int(s.attrib.get(key, 0)) for s in suites.iter('testsuite')) for key in ('tests', 'failures', 'errors', 'skipped')}
cases = list(suites.iter('testcase'))
memory = sum(c.attrib['name'].endswith('[memory]') for c in cases)
socketcan = sum(c.attrib['name'].endswith('[socketcan]') for c in cases)
c_coverage = ET.parse('artifacts/c-coverage.xml').getroot()
py_coverage = ET.parse('artifacts/python-coverage.xml').getroot()
backend = json.loads(Path('artifacts/backend.json').read_text())
metrics = {'c_unity_tests': c_tests, 'sanitizer_cases_rerun': asan_tests, 'python_tests': counts['tests'],
           'memory_bench_cases': memory, 'socketcan_bench_cases': socketcan, 'unique_tests_total': c_tests + counts['tests'],
           'python_failures': counts['failures'], 'python_errors': counts['errors'], 'python_skipped': counts['skipped'],
           'c_failures': c_failures, 'c_ignored': c_ignored, 'sanitizer_failures': asan_failures,
           'c_line_covered': int(c_coverage.attrib['lines-covered']), 'c_line_total': int(c_coverage.attrib['lines-valid']),
           'python_line_covered': int(py_coverage.attrib['lines-covered']), 'python_line_total': int(py_coverage.attrib['lines-valid']),
           'backend': backend['backend'], 'backend_reason': backend['reason'],
           'commit': os.getenv('GITHUB_SHA', 'unknown'),
           'run_url': f'https://github.com/{os.getenv("GITHUB_REPOSITORY", "hamza2829/can-bench-lab")}/actions/runs/{os.getenv("GITHUB_RUN_ID", "unknown")}'}
for prefix in ('c', 'python'):
    metrics[prefix + '_line_coverage_percent'] = round(100 * metrics[prefix + '_line_covered'] / metrics[prefix + '_line_total'], 2)
Path('artifacts/metrics.json').write_text(json.dumps(metrics, indent=2) + '\n', encoding='utf-8')
table = (f'| Metric | Measured in CI |\n| --- | ---: |\n| C Unity cases | {c_tests} |\n'
         f'| Python pytest cases, including bench | {counts["tests"]} |\n| Unique total | {metrics["unique_tests_total"]} |\n'
         f'| Memory / SocketCAN bench cases | {memory} / {socketcan} |\n'
         f'| C core line coverage | {metrics["c_line_coverage_percent"]:.2f}% ({metrics["c_line_covered"]}/{metrics["c_line_total"]}) |\n'
         f'| Python line coverage | {metrics["python_line_coverage_percent"]:.2f}% ({metrics["python_line_covered"]}/{metrics["python_line_total"]}) |\n')
Path('artifacts/metrics.md').write_text(table, encoding='utf-8')
print(json.dumps(metrics, indent=2))
if os.getenv('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as summary:
        summary.write(table)
assert not any((c_failures, c_ignored, asan_failures, asan_ignored, counts['failures'], counts['errors'], counts['skipped']))
assert asan_tests == c_tests
assert metrics['c_line_coverage_percent'] >= 80
assert memory > 0 and (backend['backend'] != 'socketcan' or socketcan > 0)

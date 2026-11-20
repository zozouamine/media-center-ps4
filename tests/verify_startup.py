#!/usr/bin/env python3
"""Exercise the console's deferred-startup path on the host, never PS4 hardware."""
import argparse
import json
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/host/ps4-media-center')
    parser.add_argument('--report', type=Path, default=ROOT / 'reports/startup-validation.json')
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.report.resolve().parent / args.report.resolve().stem
    output.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, passed, detail):
        checks.append({'name': name, 'passed': bool(passed), 'detail': detail})

    with tempfile.TemporaryDirectory(prefix='startup-validation-', dir=ROOT / 'build') as temporary:
        fixture = Path(temporary)
        mounts = fixture / 'mnt'
        mounts.mkdir()
        source = fixture / 'usb0'
        source.mkdir()
        (source / 'Videos').mkdir()
        (source / 'Music').mkdir()
        (source / 'Photos').mkdir()
        (source / 'Photos' / '\u0635\u0648\u0631\u0629.txt').write_text('UTF-8 filename fixture\n')
        script = fixture / 'controls.tsv'
        # Debug/sanitizer rendering can take several hundred milliseconds.
        # Hold each button across a render tick; retain the strict pre-0.75s
        # input assertion instead of silently missing an 80ms synthetic pulse.
        script.write_text('0\t1000\tSQUARE\n1100\t1000\tO\n2400\t1000\tSQUARE\n')
        for name, root in [('readable', source), ('absent', fixture / 'missing-usb')]:
            trace = output / (name + '.jsonl')
            run = subprocess.run([
                str(binary), '--headless', '--defer-scan', '--mount-base', str(mounts),
                '--source', str(root), '--data-dir', str(fixture / ('state-' + name)),
                '--seconds', '4.0', '--script', str(script), '--trace', str(trace),
            ], cwd=ROOT, capture_output=True, text=True, timeout=30)
            (output / (name + '.stdout.txt')).write_text(run.stdout)
            (output / (name + '.stderr.txt')).write_text(run.stderr)
            rows = [json.loads(line) for line in trace.read_text().splitlines()] if trace.exists() else []
            check(name + '_exits_cleanly', run.returncode == 0, run.returncode)
            check(name + '_ui_presented_before_discovery', bool(rows) and rows[0]['source_count'] == 0
                  and rows[0]['storage_loading'] and rows[0]['elapsed'] < 0.75, rows[0] if rows else None)
            check(name + '_controls_work_while_waiting', any(row['elapsed'] < 0.75
                  and row['modal'] == 'Details' for row in rows), 'Square opens diagnostics before scan starts')
            expected = 1 if name == 'readable' else 0
            check(name + '_scan_completes_with_expected_sources', bool(rows)
                  and rows[-1]['source_count'] == expected and not rows[-1]['storage_loading'],
                  rows[-1] if rows else None)
            check(name + '_diagnostics_remain_accessible', bool(rows) and rows[-1]['modal'] == 'Details',
                  rows[-1]['modal'] if rows else None)
        for name, flag, destination in [
            ('protected-state', '--data-dir', source / 'forbidden-state'),
            ('protected-trace', '--trace', source / 'forbidden-trace.jsonl'),
        ]:
            command = [str(binary), '--headless', '--defer-scan', '--mount-base', str(mounts),
                       '--source', str(source), '--seconds', '0.2', '--data-dir', str(fixture / name)]
            command += [flag, str(destination)]
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            check(name + '_refused_without_writing', run.returncode == 2 and not destination.exists(),
                  {'returncode': run.returncode, 'output_created': destination.exists()})

    report = {'date': datetime.now(timezone.utc).isoformat(), 'target': 'host / deferred console startup',
              'hardware_tested': False, 'checks': checks, 'passed': all(check['passed'] for check in checks)}
    args.report.resolve().write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'checks': len(checks), 'report': str(args.report.resolve())}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

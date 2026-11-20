#!/usr/bin/env python3
"""Exercise encoded formats through the real application, audio sink and UI."""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import subprocess
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tests/generated/video-formats'
OUTPUT = ROOT / 'reports/video-v11-application'
SUPPORTED_VIDEO_EXTENSIONS = {'.avi', '.mp4', '.mkv', '.mov', '.webm', '.m4v', '.mpg', '.mpeg', '.ts', '.m2ts', '.mts'}


def require(condition, message):
    if not condition: raise AssertionError(message)


def run(binary, spec):
    name, duration, width, height, sound, *extra = spec
    mode = extra[0] if extra else 'end'
    tag = name + '-' + mode
    state = ROOT / 'build/video-v11-application-state' / tag
    state.mkdir(parents=True, exist_ok=True)
    (state / 'state.tsv').write_text('setting\tvolume\t100\nsetting\tauto_next\t0\nsetting\trepeat\t0\nsetting\tshuffle\t0\n')
    trace = OUTPUT / (tag + '.jsonl'); dump = OUTPUT / (tag + '.s16le')
    seconds = 5 if mode == 'end' else 2.3
    command = [str(binary), '--headless', '--source', str(SOURCE), '--mount-base', str(ROOT / 'build/absent-video-mounts'),
               '--assets', str(ROOT / 'assets'), '--data-dir', str(state), '--open', name,
               '--seconds', str(seconds), '--trace', str(trace)]
    if sound: command += ['--audio-dump', str(dump)]
    if mode != 'end':
        script = OUTPUT / (tag + '.tsv')
        script.write_text('600 80 X\n' if mode == 'pause' else '500 80 X\n900 80 R2\n1300 80 L2\n1700 80 X\n')
        command += ['--script', str(script)]
    report = {'file': name, 'mode': mode, 'hardware_tested': False, 'passed': False}
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=seconds + 20)
        (OUTPUT / (tag + '.stderr.txt')).write_text(result.stderr)
        require(result.returncode == 0, 'Application failed: ' + result.stderr)
        final = json.loads(result.stdout.strip().splitlines()[-1])
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        require(rows and not any(row['error'] for row in rows), 'Playback reported an error')
        require(final['width'] == width and final['height'] == height, 'Wrong decoded native dimensions')
        require(final['decoded_width'] <= 1920 and final['decoded_height'] <= 1080, 'Display buffer exceeded 1080p')
        require(final['has_audio'] == bool(sound), 'Audio track was lost')
        require(final['codec'] and final['frame_revision'] > 1, 'Real decoded frames did not advance')
        require(abs(final['duration'] - duration) < .15, 'Wrong container duration')
        if mode == 'end':
            require(final['state'] == 'Ended' and abs(final['position'] - final['duration']) < .0001, 'Video did not reach its actual EOF')
            if sound:
                samples = np.frombuffer(dump.read_bytes(), dtype='<i2')
                require(abs(len(samples) / 2 / 48000 - final['duration']) < .0001, 'Output sample clock drifted from the file duration')
                rms = float(np.sqrt(np.mean(samples.astype(np.float64) ** 2)))
                require(rms > 1000, 'Decoded audio was silent')
                report['pcm'] = {'frames': len(samples) // 2, 'rate': 48000, 'channels': 2, 'rms': rms,
                                 'sha256': hashlib.sha256(dump.read_bytes()).hexdigest()}
        elif mode == 'pause':
            stable = [row for row in rows if row['elapsed'] >= 1.1]
            require(stable and all(row['state'] == 'Paused' for row in stable), 'Pause was not retained')
            require(len({row['position'] for row in stable}) == 1, 'Paused audio clock advanced')
            require(len({row['frame_revision'] for row in stable}) == 1, 'Paused video frames advanced')
        else:
            forward = [row for row in rows if 1.05 <= row['elapsed'] <= 1.2]
            backward = [row for row in rows if 1.45 <= row['elapsed'] <= 1.6]
            require(forward and backward, 'Missing seek observations')
            require(all(row['state'] == 'Paused' and abs(row['position'] - duration) < .15 for row in forward), 'Forward seek lost the pause/target')
            require(all(row['state'] == 'Paused' and row['position'] == 0 for row in backward), 'Backward seek lost the pause/target')
            require(final['state'] == 'Playing' and final['position'] > 0, 'Resume after seeking failed')
        report.update(passed=True, final=final, trace=trace.relative_to(ROOT).as_posix())
    except Exception as error:
        report['error'] = str(error)
    print(('PASS ' if report['passed'] else 'FAIL ') + tag + (' ' + report.get('error', '') if not report['passed'] else ''), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2); args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    specs = []
    for line in (SOURCE / 'fixtures.tsv').read_text().splitlines():
        name, duration, width, height, sound = line.split('\t')
        if Path(name).suffix.lower() in SUPPORTED_VIDEO_EXTENSIONS:
            specs.append((name, float(duration), int(width), int(height), int(sound)))
    baseline = specs[0]; specs += [(*baseline, 'pause'), (*baseline, 'seek')]
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCE.iterdir() if p.is_file()}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(2, args.jobs))) as pool:
        results = list(pool.map(lambda spec: run(args.binary.resolve(), spec), specs))
    after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCE.iterdir() if p.is_file()}
    results.append({'file': 'read-only source hashes', 'passed': before == after})
    report = {'date': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'target': 'host application',
              'hardware_tested': False, 'passed': all(result['passed'] for result in results), 'cases': results}
    (ROOT / 'reports/video-v11-application-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())

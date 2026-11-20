#!/usr/bin/env python3
"""Run the real Cocoa/CoreAudio window and sample its resident memory."""
import argparse
import datetime
import hashlib
import json
import subprocess
import time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/host/ps4-media-center')
    parser.add_argument('--seconds', type=float, default=20)
    args = parser.parse_args()
    binary = args.binary.resolve()
    state = ROOT / 'build/native-validation-state'
    state.mkdir(parents=True, exist_ok=True)
    # Consume real output callbacks silently while testing the playback clock.
    (state / 'state.tsv').write_text('setting\tvolume\t0\nsetting\tauto_next\t0\nsetting\trepeat\t0\nsetting\tshuffle\t0\n')
    script = ROOT / 'reports/native-music-controls.tsv'
    script.write_text('1000 200 X\n2000 200 R2\n3000 200 L2\n4000 200 X\n')
    trace = ROOT / 'reports/native-music-final.jsonl'
    capture = ROOT / 'reports/native-music-final.bmp'
    command = [str(binary), '--source', str(ROOT / 'tests/generated/usb-demo'),
               '--assets', str(ROOT / 'assets'), '--data-dir', str(state),
               '--open', 'Music/Playlist/01 - First track.wav', '--seconds', str(args.seconds),
               '--script', str(script), '--trace', str(trace), '--screenshot', str(capture)]
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    samples = []
    while process.poll() is None:
        elapsed = time.monotonic() - started
        if elapsed > args.seconds + 15:
            process.kill()
            break
        measured = subprocess.run(['/bin/ps', '-o', 'rss=', '-p', str(process.pid)], capture_output=True, text=True)
        if measured.returncode == 0 and measured.stdout.strip().isdigit():
            samples.append(dict(elapsed=round(elapsed, 3), rss_kib=int(measured.stdout.strip())))
        time.sleep(.5)
    stdout, stderr = process.communicate(timeout=5)
    report = dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  target='native macOS Cocoa / CoreAudio', hardware_tested=False, emulator_tested=False,
                  command=command, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  exit_code=process.returncode, wall_seconds=time.monotonic()-started,
                  memory_samples=samples, stderr=stderr, passed=False)
    try:
        assert process.returncode == 0, stderr or f'Native application exited {process.returncode}'
        final = json.loads(stdout.strip().splitlines()[-1])
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        report['final'] = final
        assert rows and all(row['target'] == 'native macOS' for row in rows), 'Missing actual Cocoa telemetry'
        assert final['screen'] == 'Music' and final['state'] == 'Playing' and not final['error'], 'Native music did not resume'
        assert final['artwork_decoded'] and final['artwork_width'] == final['artwork_height'] == 512, 'Native artwork was lost'
        assert final['position'] > args.seconds - 6 and final['duration'] == 35, 'Real audio callback clock did not advance'
        paused = [row for row in rows if 1.4 <= row['elapsed'] <= 1.8]
        ahead = [row for row in rows if 2.4 <= row['elapsed'] <= 2.8]
        back = [row for row in rows if 3.4 <= row['elapsed'] <= 3.8]
        assert paused and ahead and back and all(row['state'] == 'Paused' for row in paused+ahead+back), 'Native pause input was missed'
        assert abs(ahead[-1]['position'] - paused[-1]['position'] - 10) < .03, 'Native forward seek failed'
        assert abs(back[-1]['position'] - paused[-1]['position']) < .03, 'Native backward seek failed'
        assert len({row['session'] for row in rows if row['elapsed'] > .5}) == 1
        early = [sample['rss_kib'] for sample in samples if 3 <= sample['elapsed'] <= 5]
        late = [sample['rss_kib'] for sample in samples if sample['elapsed'] >= args.seconds-3]
        assert early and late, 'Native memory sampling unavailable'
        growth = max(late)-min(early)
        peak = max(sample['rss_kib'] for sample in samples)
        assert growth < 48*1024, f'Cocoa drawing memory grew {growth/1024:.1f} MiB'
        assert peak < 256*1024, f'Native resident memory reached {peak/1024:.1f} MiB'
        report.update(final=final, peak_rss_mib=peak/1024, steady_growth_mib=growth/1024,
                      real_seek='PASS', real_audio_callback_clock='PASS', autorelease_pool_memory_check='PASS')
        with Image.open(capture) as bitmap:
            assert bitmap.size == (1920, 1080)
            bitmap.save(capture.with_suffix('.png'))
        capture.unlink()
        report['screenshot'] = 'reports/native-music-final.png'
        report['passed'] = True
    except Exception as error:
        report['failure'] = str(error)
    (ROOT / 'reports/native-music-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({key:value for key,value in report.items() if key not in ['memory_samples','command']}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

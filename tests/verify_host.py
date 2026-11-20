#!/usr/bin/env python3
"""Exercise the actual native application's shared renderer and playback workers."""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
USB = ROOT / "tests/generated/usb-demo"
OUTPUT = ROOT / "reports/host-validation"
STATE = ROOT / "build/host-validation-state"


def source_snapshot():
    result = {}
    for folder, directories, names, descriptor in os.fwalk(USB, follow_symlinks=False):
        for name in directories + names:
            st = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
            relative = os.path.relpath(os.path.join(folder, name), USB)
            result[relative] = (st.st_mode, st.st_size, st.st_mtime_ns)
    return result


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def case(name, seconds=1.1, media=None, script="", screenshot=False, pcm=False, language=0, select=None, validator=None, exit_code=0):
    return dict(name=name, seconds=seconds, media=media, script=script, screenshot=screenshot,
                pcm=pcm, language=language, select=select, validator=validator, exit_code=exit_code)


def run_case(binary, spec):
    name = spec["name"]
    state = STATE / name
    state.mkdir(parents=True, exist_ok=True)
    (state / "state.tsv").write_text("setting\tvolume\t100\nsetting\tauto_next\t0\nsetting\trepeat\t0\n"
                                   f"setting\tshuffle\t0\nsetting\tlanguage\t{spec['language']}\n", encoding="utf-8")
    trace = OUTPUT / (name + ".jsonl")
    command = [str(binary), "--headless", "--source", str(USB), "--data-dir", str(state),
               "--assets", str(ROOT / "assets"), "--seconds", str(spec["seconds"]), "--trace", str(trace)]
    if spec["media"]:
        command += ["--open", spec["media"]]
    if spec["select"]:
        command += ["--select", spec["select"]]
    if spec["script"]:
        path = OUTPUT / (name + ".tsv")
        path.write_text(spec["script"], encoding="utf-8")
        command += ["--script", str(path)]
    dump = OUTPUT / (name + ".s16le")
    if spec["pcm"]:
        command += ["--audio-dump", str(dump)]
    bmp = OUTPUT / (name + ".bmp")
    if spec["screenshot"]:
        command += ["--screenshot", str(bmp)]
    launched = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=spec["seconds"] + 12)
    except subprocess.TimeoutExpired as error:
        failure = dict(name=name, target="computer / shared native application core", hardware_tested=False,
                       command=command, passed=False, failure=f"Application exceeded the {error.timeout}-second deadline")
        print("FAIL " + name + ": " + failure["failure"], flush=True)
        return failure
    report = dict(name=name, target="computer / shared native application core", hardware_tested=False,
                  command=command, exit_code=result.returncode, wall_seconds=time.monotonic()-launched, stderr=result.stderr, passed=False)
    (OUTPUT / (name + ".stdout.txt")).write_text(result.stdout, encoding="utf-8")
    (OUTPUT / (name + ".stderr.txt")).write_text(result.stderr, encoding="utf-8")
    try:
        check(result.returncode == spec["exit_code"], f"Unexpected exit status {result.returncode}: {result.stderr}")
        final = json.loads(result.stdout.strip().splitlines()[-1])
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        report["final"] = final
        report["trace"] = trace.relative_to(ROOT).as_posix()
        check(rows and all(r["target"] == "host headless" for r in rows), "Missing real application telemetry")
        if spec["validator"]:
            spec["validator"](final, rows, dump, report)
        if spec["screenshot"]:
            with Image.open(bmp) as image:
                check(image.size == (1920, 1080), "Incorrect framebuffer dimensions")
                png = bmp.with_suffix(".png")
                image.save(png)
            report["screenshot"] = png.relative_to(ROOT).as_posix()
            bmp.unlink()
        report["passed"] = True
    except Exception as error:
        report["failure"] = str(error)
    print(("PASS " if report["passed"] else "FAIL ") + name +
          (": " + report.get("failure", "") if not report["passed"] else ""), flush=True)
    return report


def audio(relative):
    def validate(final, rows, dump, report):
        check(final["state"] == "Ended" and not final["error"], "Audio did not reach a clean EOF")
        check(1.99 <= final["duration"] <= 2.10, "Wrong decoded audio duration")
        raw = dump.read_bytes()
        check(len(raw) % 4 == 0, "Incomplete stereo sample frame")
        samples = np.frombuffer(raw, dtype="<i2")
        frames = len(samples) // 2
        check(abs(frames / 48000 - final["duration"]) < .003, "Output sample clock does not match playback duration")
        rms = float(np.sqrt(np.mean(samples.astype(np.float64) ** 2)))
        check(rms > 1000, "Captured decoder output was silent")
        if relative in ("Music/Tone.wav", "Music/Tone.flac"):
            check(raw == (ROOT / "tests/generated/expected-stereo.s16le").read_bytes(), "Lossless PCM differs from the fixture")
        if relative == "Music/Mono44100.wav":
            check(np.array_equal(samples[0::2], samples[1::2]), "Mono audio was not mapped to both output channels")
        report["pcm"] = dict(frames=frames, sample_rate=48000, channels=2, rms=rms, sha256=hashlib.sha256(raw).hexdigest())
    return validate


def image(final, rows, dump, report):
    check(final["screen"] == "Photo" and final["photo_decoded"] and not final["photo_error"], "Image pixels were not decoded")
    check((final["photo_width"], final["photo_height"]) == (120, 80), "Wrong image dimensions")


def failure(final, rows, dump, report):
    check(final["error"] or final["photo_error"], "Unsupported/corrupt media lacked a real error")
    check(final["state"] == "Error" or (final["screen"] == "Photo" and not final["photo_decoded"]), "Failed media appeared playable")


def video_decoded(final, rows, dump, report):
    check(final["screen"] == "Video" and final["state"] in ("Playing", "Ended") and not final["error"], "H.264 MP4 did not play")
    check(final["width"] == 96 and final["height"] == 64 and final["frame_revision"] > 1, "Real H.264 frames did not advance")


def osd_hidden(final, rows, dump, report):
    check(final["state"] == "Playing" and not final["osd_visible"], "OSD did not hide while video continued")
    hidden = [r for r in rows if r["state"] == "Playing" and not r["osd_visible"]]
    check(hidden and 7.0 <= hidden[0]["elapsed"] <= 7.8, "Incorrect first seven-second timeout")
    check(final["position"] > 5 and final["frame_revision"] > 60, "Video did not advance during OSD timeout")
    report["first_hidden_elapsed"] = hidden[0]["elapsed"]


def osd_cycle(final, rows, dump, report):
    hidden_first = [r for r in rows if 7.5 <= r["elapsed"] <= 8.8]
    revealed = [r for r in rows if 9.02 <= r["elapsed"] <= 9.5]
    hidden_again = [r for r in rows if 16.4 <= r["elapsed"]]
    check(hidden_first and all(not r["osd_visible"] for r in hidden_first), "Dead-zone drift revealed the OSD")
    check(revealed and all(r["osd_visible"] for r in revealed), "Intentional right-stick movement did not reveal OSD")
    check(hidden_again and all(not r["osd_visible"] for r in hidden_again), "OSD failed to hide seven seconds after renewed input")
    active = [r for r in rows if r["elapsed"] >= 1]
    check(all(r["state"] == "Playing" for r in active), "OSD interaction interrupted video playback")
    check(len({r["session"] for r in active}) == 1, "OSD interaction reopened the decoder")
    check(hidden_again[-1]["position"] > revealed[0]["position"] + 5, "Video sample clock stopped while OSD was visible/hidden")
    check(hidden_again[-1]["frame_revision"] > revealed[0]["frame_revision"] + 40, "Real video frames stopped advancing")
    report["observations"] = dict(first_hidden=hidden_first[0], stick_reveal=revealed[0], second_hidden=hidden_again[0])


def paused(final, rows, dump, report):
    check(final["state"] == "Paused" and final["osd_visible"], "Paused video controls did not remain visible")
    stable = [r for r in rows if r["elapsed"] >= 1.2]
    check(stable and len({r["position"] for r in stable}) == 1, "Paused audio clock advanced")
    check(len({r["frame_revision"] for r in stable}) == 1, "Paused video continued decoding")
    check(all(r["osd_visible"] for r in stable), "Paused OSD timed out")


def seek(final, rows, dump, report):
    check(final["state"] == "Playing" and 10 < final["position"] < 12.5, "Seek did not change the actual playback position")
    before = [r for r in rows if .7 <= r["elapsed"] < .9]
    after = [r for r in rows if 1.04 <= r["elapsed"] < 1.3]
    check(before and after and before[-1]["state"] == after[0]["state"] == "Paused", "Seek did not preserve pause")
    check(after[0]["position"] > before[-1]["position"] + 9.8, "R2 did not seek ten seconds")
    check(after[0]["frame_revision"] > before[-1]["frame_revision"], "Seek did not decode the target video frame")


def video_controls(final, rows, dump, report):
    check(final["screen"] == "Video" and final["state"] == "Paused" and final["osd_visible"], "Video controls lost the paused player")
    before = [r for r in rows if .95 <= r["elapsed"] < 1.15]
    after = [r for r in rows if r["elapsed"] >= 1.65]
    check(before and after and before[-1]["state"] == after[0]["state"] == "Paused", "Video seek did not preserve pause")
    check(after[0]["position"] > before[-1]["position"] + 9.9, "Video forward did not seek ten seconds")
    check(after[0]["frame_revision"] > before[-1]["frame_revision"], "Video control did not decode a new frame")
    check(len({r["session"] for r in before+after}) == 1, "Video seek reopened its playback session")


def browser(final, rows, dump, report):
    check(final["screen"] == "Browser" and final["library"] == "Directory", "Browser failed to show a real source")
    check(final["ui_language"] == "English", "A legacy language setting changed the English-only interface")
    check(final["space_known"] and final["total_bytes"] > 0 and 0 <= final["available_bytes"] <= final["total_bytes"], "Storage capacity was not read from the source")
    check(final["source_label"] and final["source_label"] != "usb-demo", "Source still uses the fixture folder name instead of its volume label")


def category(tab, folder):
    def validate(final, rows, dump, report):
        browser(final, rows, dump, report)
        check(final["tab"] == tab and final["directory"] == folder, "Category did not open its named source folder")
    return validate


def animated(final, rows, dump, report):
    image(final, rows, dump, report)
    check(final["photo_frames"] == 3 and not final["animation_paused"], "Animation did not retain its three frames")
    check({r["photo_frame"] for r in rows if r["photo_decoded"]} == {0, 1, 2}, "Actual photo viewer did not display every animation frame")


def animation_paused(final, rows, dump, report):
    image(final, rows, dump, report)
    stable = [r for r in rows if r["elapsed"] >= 1.35]
    check(stable and all(r["animation_paused"] for r in stable), "Photo Options did not pause GIF animation")
    check(len({r["photo_frame"] for r in stable}) == 1, "Paused GIF continued changing frames")


def artwork(width=120, height=80, arabic=False):
    def validate(final, rows, dump, report):
        check(final["screen"] == "Music" and final["state"] in ("Playing", "Paused") and not final["error"], "Artwork audio did not play")
        check(final["artwork_decoded"] and (final["artwork_width"], final["artwork_height"]) == (width, height), "Embedded artwork was not decoded")
        check(final["position"] > 0 and final["duration"] >= 1.9, "Music progress did not come from the audio clock")
        if arabic:
            check(final["media_title"] == "موسيقى عربية" and final["ui_language"] == "English", "Arabic metadata or English UI was lost")
    return validate


def music_seek(final, rows, dump, report):
    def sample(start, end):
        found = [r for r in rows if start <= r["elapsed"] <= end]
        check(found, "Missing music seek sample")
        return found[-1]
    before, ahead, back = sample(.85, 1.0), sample(1.25, 1.4), sample(1.65, 1.8)
    check(before["state"] == ahead["state"] == back["state"] == "Paused", "Music seek did not preserve pause")
    check(abs(ahead["position"] - before["position"] - 10) < .02, "Music forward did not seek ten seconds")
    check(abs(back["position"] - before["position"]) < .02, "Music rewind did not return ten seconds")
    check(final["state"] == "Playing" and final["position"] > before["position"] + 10, "Music did not resume from its seek position")
    check(len({r["session"] for r in (before, ahead, back, final)}) == 1, "Seek reopened the track")


def music_back(final, rows, dump, report):
    active = [r for r in rows if r["state"] == "Playing"]
    check(final["screen"] == "Browser" and final["state"] == "Playing", "Back did not return to the library with music continuing")
    check(active and len({r["session"] for r in active}) == 1 and final["position"] > active[0]["position"] + .7, "Back interrupted the actual music clock")


def music_stop(final, rows, dump, report):
    check(any(r["screen"] == "Music" and r["state"] == "Playing" for r in rows), "Music did not start before Stop")
    check(final["screen"] == "Browser" and final["state"] == "Idle" and not final["artwork_decoded"], "Stop did not release audio and artwork")


def music_playlist(final, rows, dump, report):
    tracks = {r["media_name"] for r in rows if r["state"] in ("Playing", "Paused")}
    check(tracks == {"01 - First track.wav", "02 - Next track.wav"}, "Previous/Next did not open the real playlist tracks")
    check(final["screen"] == "Music" and final["state"] == "Paused" and final["playlist_count"] == 2 and final["playlist_index"] == 0, "Music playlist focus or pause was lost")
    check(final["artwork_decoded"] and final["artwork_width"] == 512, "Playlist cover art was lost")


def cannot_open(final, rows, dump, report):
    browser(final, rows, dump, report)
    check(final["selected_name"] == "Readme.txt" and final["state"] == "Idle" and "Cannot open" in final["notice"], "Nonmedia entry appeared playable")


def search(final, rows, dump, report):
    check(final["modal"] == "Search", "Controller navigation did not open the search keyboard")


def details(final, rows, dump, report):
    check(final["modal"] == "Details", "Square did not open real file information")


def photo_controls(final, rows, dump, report):
    check(final["photo_decoded"] and not final["photo_error"], "Photo controls lost the decoded image")
    check(final["rotation"] == 1 and final["zoom"] == 1.25 and final["slideshow"], "Rotation, zoom or slideshow controls did not act")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=ROOT / "build/host/ps4-media-center")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--case", action="append", dest="selected_cases")
    args = parser.parse_args()
    binary = args.binary.resolve()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    STATE.mkdir(parents=True, exist_ok=True)
    before = source_snapshot()
    specs = [
        case("h264-playback", 1.4, "Clips/Unsupported.mp4", validator=video_decoded),
        case("video-osd-cycle", 16.8, "Clips/OSD timing.avi",
             "7400 900 NONE 0.10 0.10 0.09 0.10 0 0\n9000 200 NONE 0 0 0.60 0 0 0\n",
             screenshot=True, validator=osd_cycle),
        case("video-osd-hidden", 7.8, "Sample Video.avi", screenshot=True, validator=osd_hidden),
        case("video-paused", 8.6, "Sample Video.avi", "600 80 X\n", screenshot=True, validator=paused),
        case("video-seek", 2.3, "Sample Video.avi", "500 80 X\n900 120 R2\n1300 80 X\n", screenshot=True, validator=seek),
        case("video-controls-final", 2.2, "Sample Video.avi", "700 200 X\n1250 200 R2\n", screenshot=True, validator=video_controls),
    ]
    for relative in ("Music/Tone.wav", "Music/Tone.mp3", "Music/Tone.flac", "Music/Tone.ogg", "Music/Mono44100.wav",
                     "Music/Tone.aac", "Music/Tone.m4a", "Music/Tone.opus", "Music/Tone.wma", "Music/Tone.aiff"):
        specs.append(case("audio-" + Path(relative).stem.lower() + "-" + Path(relative).suffix[1:],
                          3.1, relative, pcm=True, validator=audio(relative)))
    for suffix in ("png", "bmp", "jpg", "webp", "tga", "ppm", "pgm", "pnm", "psd", "hdr", "tiff"):
        specs.append(case("image-" + suffix, media="Photos/Pattern." + suffix, screenshot=suffix == "png", validator=image))
    specs += [
        case("image-webp-lossless", media="Photos/Pattern lossless.webp", validator=image),
        case("image-tiff-lzw", media="Photos/Pattern LZW.tif", validator=image),
        case("image-tiff-packbits", media="Photos/Pattern PackBits.tif", validator=image),
        case("image-tiff-deflate", media="Photos/Pattern Deflate.tif", validator=image),
        case("image-tiff-ccitt", media="Photos/Monochrome Group4.tif", validator=image),
        case("image-ico-png", media="Photos/PNG icon.ico", validator=image),
        case("image-ico-bitmap", media="Photos/Bitmap icon.ico", validator=image),
        case("image-gif-animated", 1.6, "Photos/Animated.gif", validator=animated),
        case("image-webp-animated", 1.6, "Photos/Animated.webp", validator=animated),
        case("image-gif-paused", 2.1, "Photos/Animated.gif", "350 40 OPTIONS\n480 40 DOWN\n610 40 DOWN\n740 40 DOWN\n870 40 DOWN\n1000 40 DOWN\n1130 40 X\n", validator=animation_paused),
        case("category-videos", script="350 60 R1\n", validator=category(1, "Videos")),
        case("category-music", script="350 60 R1\n550 60 R1\n", validator=category(2, "Music")),
        case("category-photos", script="350 60 R1\n550 60 R1\n750 60 R1\n", validator=category(3, "Photos")),
        case("file-cannot-open", select="Readme.txt", script="500 60 X\n", screenshot=True, validator=cannot_open),
        case("music-artwork-arabic", 1.3, "Music/Artwork Arabic.mp3", validator=artwork(arabic=True)),
        case("music-player-final", 1.6, "Music/Playlist/01 - First track.wav", "650 60 X\n", screenshot=True, validator=artwork(512, 512)),
        case("music-seek", 3.0, "Music/Playlist/01 - First track.wav", "650 60 X\n1100 60 R2\n1500 60 L2\n1850 60 R2\n2300 60 X\n", validator=music_seek),
        case("music-back", 1.9, "Music/Playlist/01 - First track.wav", "1000 200 O\n", validator=music_back),
        case("music-stop", 1.6, "Music/Playlist/01 - First track.wav", "1000 60 SQUARE\n", validator=music_stop),
        case("music-playlist", 3.4, script="350 60 R1\n550 60 R1\n800 60 X\n1050 60 X\n1450 60 X\n1900 60 R1\n2350 60 L1\n2750 60 X\n3050 60 R2\n", screenshot=True, validator=music_playlist),
        case("image-arabic", media="Photos/صورة عربية.png", screenshot=True, language=1, validator=image),
        case("photo-controls", 2.6, "Nature/Lake.png",
             "350 100 SQUARE\n550 100 UP\n850 100 OPTIONS\n1050 100 DOWN\n1250 100 X\n1550 100 X\n",
             screenshot=True, validator=photo_controls),
        case("interface", select="Sample Video.avi", screenshot=True, validator=browser),
        case("arabic-browser", select="ملفات عربية", language=1, screenshot=True, validator=browser),
        case("file-details", select="Sample Video.avi", script="500 80 SQUARE\n", screenshot=True, validator=details),
        case("arabic-keyboard", script="350 60 OPTIONS\n480 60 DOWN\n610 60 X\n750 60 TRIANGLE\n",
             screenshot=True, validator=search),
    ]
    for suffix in ("wav", "mp3", "flac", "ogg"):
        specs.append(case("music-artwork-" + suffix, 1.3, "Music/Artwork." + suffix, validator=artwork()))
    for name, relative in (("corrupt-gif", "Corrupt/Truncated.gif"), ("gif-memory-limit", "Corrupt/Animation memory limit.gif"),
                           ("gif-frame-limit", "Corrupt/Too many frames.gif"), ("corrupt-webp", "Corrupt/Bad.webp"),
                           ("webp-frame-limit", "Corrupt/Too many frames.webp"), ("corrupt-ico", "Corrupt/Bad.ico"),
                           ("corrupt-tiff", "Corrupt/Bad.tiff"), ("tiff-jpeg-unavailable", "Corrupt/Unsupported JPEG TIFF.tif"),
                           ("corrupt-image", "Corrupt/bad.png"),
                           ("corrupt-audio", "Corrupt/bad.wav"), ("corrupt-video", "Corrupt/bad.avi")):
        specs.append(case(name, .9, relative, validator=failure, exit_code=1))
    if args.selected_cases:
        specs = [spec for spec in specs if spec["name"] in args.selected_cases]
        check(len(specs) == len(set(args.selected_cases)), "Unknown selected case")
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(3, args.jobs))) as pool:
        reports = list(pool.map(lambda spec: run_case(binary, spec), specs))
    protected = USB / "must-not-be-created"
    result = subprocess.run([str(binary), "--headless", "--source", str(USB), "--data-dir", str(protected)],
                            capture_output=True, text=True, cwd=ROOT, timeout=5)
    protected_ok = result.returncode == 2 and not protected.exists()
    reports.append(dict(name="read-only-output-protection", passed=protected_ok, exit_code=result.returncode, stderr=result.stderr))
    unchanged = before == source_snapshot()
    reports.append(dict(name="source-tree-unchanged", passed=unchanged, entries_checked=len(before)))
    payload = dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(), target="computer", hardware_tested=False,
                   emulator_tested=False, passed=all(r["passed"] for r in reports), cases=reports)
    filename = "host-validation-selected.json" if args.selected_cases else "host-validation.json"
    (ROOT / "reports" / filename).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{sum(r['passed'] for r in reports)}/{len(reports)} host application checks passed.", flush=True)
    return 0 if payload["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

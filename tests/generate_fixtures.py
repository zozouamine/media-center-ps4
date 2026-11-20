#!/usr/bin/env python3
"""Generate actual, deterministic media fixtures; never used as product content."""
import hashlib
import json
import os
import shutil
import struct
import wave
import zlib
from fractions import Fraction
from pathlib import Path
import av
import numpy as np
from PIL import Image
from extend_fixtures import extend
from generate_video_fixtures import main as generate_video_fixtures

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'tests/generated'
USB = BASE / 'usb-demo'


def pcm(rate=48000, seconds=2, channels=2):
    t = np.arange(int(rate * seconds), dtype=np.float64) / rate
    a = (np.sin(t * 2 * np.pi * 440) * 11000).astype(np.int16)
    if channels == 1:
        return a.reshape(1, -1)
    b = (np.sin(t * 2 * np.pi * 660) * 7000).astype(np.int16)
    return np.stack([a, b])


def encode_audio(path, codec, container=None, metadata=None):
    output = av.open(str(path), 'w', format=container)
    if metadata:
        output.metadata.update(metadata)
    stream = output.add_stream(codec, rate=48000)
    stream.layout = 'stereo'
    if codec in ('aac', 'libopus', 'wmav2'):
        stream.bit_rate = 128000
    if codec == 'vorbis':
        stream.options = {'strict': '-2'}
    signal = pcm()
    for begin in range(0, signal.shape[1], 1024):
        block = signal[:, begin:begin+1024]
        frame = av.AudioFrame.from_ndarray(block, format='s16p', layout='stereo')
        frame.sample_rate = 48000; frame.pts = begin; frame.time_base = Fraction(1, 48000)
        for packet in stream.encode(frame):
            output.mux(packet)
    for packet in stream.encode(None):
        output.mux(packet)
    output.close()


def encode_video(path, codec, width, height, count, fps, sound=False, container='avi'):
    output = av.open(str(path), 'w', format=container)
    video = output.add_stream(codec, rate=fps)
    video.width = width; video.height = height
    video.pix_fmt = 'yuvj420p' if codec == 'mjpeg' else 'bgr24' if codec == 'rawvideo' else 'yuv420p'
    audio = output.add_stream('pcm_s16le', rate=48000) if sound else None
    if audio:
        audio.layout = 'stereo'
    signal = pcm(seconds=count / fps)
    xx, yy = np.meshgrid(np.arange(width), np.arange(height))
    for index in range(count):
        image = np.stack([(xx * 2 + index * 9) % 256, (yy * 3 + index * 5) % 256,
                          ((xx + yy) + index * 13) % 256], axis=-1).astype(np.uint8)
        frame = av.VideoFrame.from_ndarray(image, format='rgb24'); frame.pts = index
        for packet in video.encode(frame):
            output.mux(packet)
        if audio:
            begin = index * 48000 // fps; end = (index + 1) * 48000 // fps
            block = av.AudioFrame.from_ndarray(signal[:, begin:end], format='s16p', layout='stereo')
            block.sample_rate = 48000; block.pts = begin; block.time_base = Fraction(1, 48000)
            for packet in audio.encode(block):
                output.mux(packet)
    for packet in video.encode(None):
        output.mux(packet)
    if audio:
        for packet in audio.encode(None):
            output.mux(packet)
    output.close()


def main():
    generate_video_fixtures()
    if (BASE / 'manifest.json').exists():
        timing = USB / 'Clips/OSD timing.avi'
        if not timing.exists():
            encode_video(timing, 'mjpeg', 160, 90, 240, 12, True)
            manifest = json.loads((BASE / 'manifest.json').read_text())
            manifest['files'].append({'path': timing.relative_to(USB).as_posix(), 'bytes': timing.stat().st_size, 'sha256': hashlib.sha256(timing.read_bytes()).hexdigest()})
            (BASE / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        extend(USB, BASE, ROOT, encode_audio)
        print('Existing generated fixture set retained.')
        return
    USB.mkdir(parents=True, exist_ok=True)
    for folder in ['Movies', 'Series', 'Anime', 'Clips', 'Nature', 'Music', 'Photos', 'ملفات عربية', 'Large', 'Corrupt']:
        (USB / folder).mkdir(exist_ok=True)
    rgb = np.zeros((80, 120, 3), dtype=np.uint8)
    rgb[:, :, 0] = np.arange(120); rgb[:, :, 1] = np.arange(80).reshape(-1, 1); rgb[:, :, 2] = 173
    image = Image.fromarray(rgb)
    for name in ['Pattern.png', 'Pattern.bmp', 'Pattern.jpg', 'صورة عربية.png', 'Pattern.webp']:
        image.save(USB / 'Photos' / name)
    reference = ROOT / 'PS4_Media_Center_Design_Reference.png'
    if reference.exists():
        r = Image.open(reference).convert('RGB'); w, h = r.size
        r.crop((int(w * .616), int(h * .244), int(w * .962), int(h * .583))).save(USB / 'Nature/Lake.png')
    stereo = pcm().T.copy()
    with wave.open(str(USB / 'Music/Tone.wav'), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000); w.writeframes(stereo.tobytes())
    (BASE / 'expected-stereo.s16le').write_bytes(stereo.tobytes())
    with wave.open(str(USB / 'Music/Mono44100.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100); w.writeframes(pcm(44100, 2, 1).T.copy().tobytes())
    encode_audio(USB / 'Music/Tone.mp3', 'libmp3lame', metadata={'title': 'Generated tone', 'artist': 'PS4MC test suite'})
    encode_audio(USB / 'Music/Tone.flac', 'flac')
    encode_audio(USB / 'Music/Tone.ogg', 'vorbis', 'ogg', {'title': 'Generated Vorbis tone'})
    encode_audio(USB / 'Music/Unsupported.m4a', 'aac', 'ipod')
    encode_video(USB / 'Sample Video.avi', 'mjpeg', 320, 180, 300, 24, True)
    encode_video(USB / 'Clips/OSD timing.avi', 'mjpeg', 160, 90, 240, 12, True)
    encode_video(USB / 'Movies/Short RGB.avi', 'rawvideo', 64, 48, 20, 10)
    encode_video(USB / 'Movies/Silent MJPEG.avi', 'mjpeg', 96, 64, 24, 12)
    encode_video(USB / 'Clips/Unsupported.mp4', 'libx264', 96, 64, 12, 12, False, 'mp4')
    (USB / 'ملفات عربية/ملف بمسافات.txt').write_text('UTF-8 fixture\n', encoding='utf-8')
    (USB / '.hidden.txt').write_text('Hidden fixture\n')
    for i in range(4205):
        (USB / 'Large' / f'item_{i:05d}.wav').touch()
    for suffix in ['avi', 'png', 'wav', 'mp3', 'flac', 'ogg']:
        (USB / 'Corrupt' / ('empty.' + suffix)).touch()
        (USB / 'Corrupt' / ('bad.' + suffix)).write_bytes(b'corrupt media fixture\x00\xff')
    (USB / 'Corrupt/truncated.avi').write_bytes((USB / 'Sample Video.avi').read_bytes()[:73])
    malformed = b'RIFF' + struct.pack('<I', 0xffffffff) + b'AVI '
    (USB / 'Corrupt/oversized.avi').write_bytes(malformed)
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)
    bomb = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 9000, 9000, 8, 2, 0, 0, 0)) + chunk(b'IEND', b'')
    (USB / 'Corrupt/too-large.png').write_bytes(bomb)
    # Construct a >1024-byte path via directory descriptors, not one giant syscall path.
    deep = USB / 'Deep'; deep.mkdir(exist_ok=True); fd = os.open(deep, os.O_RDONLY)
    components = []
    try:
        for i in range(8):
            name = f'{i}_' + 'x' * 190; components.append(name)
            try:
                os.mkdir(name, dir_fd=fd)
            except FileExistsError:
                pass
            child = os.open(name, os.O_RDONLY, dir_fd=fd); os.close(fd); fd = child
        leaf = os.open('long name ملف.txt', os.O_CREAT | os.O_WRONLY, 0o600, dir_fd=fd)
        os.write(leaf, b'long-path-ok'); os.close(leaf)
    finally:
        os.close(fd)
    (BASE / 'long-relative.txt').write_text('Deep/' + '/'.join(components) + '/long name ملف.txt')
    outside = BASE / 'outside.txt'; outside.write_text('outside source\n')
    for name in ['escape-link.txt', 'escape-dir']:
        p = USB / name
        if p.is_symlink(): p.unlink()
    (USB / 'escape-link.txt').symlink_to(outside)
    (USB / 'escape-dir').symlink_to(BASE, target_is_directory=True)
    files = []
    for folder, dirs, names in os.walk(USB, followlinks=False):
        dirs[:] = [d for d in dirs if d not in ['Large', 'Deep'] and not (Path(folder) / d).is_symlink()]
        for name in sorted(names):
            p = Path(folder) / name
            if not p.is_symlink():
                files.append({'path': p.relative_to(USB).as_posix(), 'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
    manifest = {'generator': 'PyAV 18.1.0 / Pillow / NumPy; synthetic test files only',
                'source': str(USB), 'large_directory_entries': 4205, 'long_relative_bytes': len((BASE / 'long-relative.txt').read_text().encode()), 'files': files}
    (BASE / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    extend(USB, BASE, ROOT, encode_audio)
    print(json.dumps({k: v for k, v in manifest.items() if k != 'files'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

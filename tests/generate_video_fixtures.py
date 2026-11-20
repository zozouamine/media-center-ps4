#!/usr/bin/env python3
"""Encode real containers/codecs and independent decoded frame/PCM references."""
import hashlib
import json
from fractions import Fraction
from pathlib import Path
import av
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'tests/generated/video-formats'
VERSION = 2
SPECS = [
    ('h264-aac.mp4', 'mp4', 'libx264', 'aac', {'chirp': True}),
    ('hevc-aac.mkv', 'matroska', 'libx265', 'aac'),
    ('hevc10-flac.mkv', 'matroska', 'libx265', 'flac', {'pix_fmt': 'yuv420p10le'}),
    ('mpeg4-mp3.avi', 'avi', 'mpeg4', 'libmp3lame'),
    ('mpeg2-mp2.mpg', 'mpeg', 'mpeg2video', 'mp2', {'fps': 25}),
    ('h264-aac.ts', 'mpegts', 'libx264', 'aac'),
    ('h264-ac3.m2ts', 'mpegts', 'libx264', 'ac3'),
    ('h264-eac3.mkv', 'matroska', 'libx264', 'eac3', {'video_offset': .5}),
    ('vp8-vorbis.webm', 'webm', 'libvpx', 'vorbis'),
    ('vp9-opus.webm', 'webm', 'libvpx-vp9', 'libopus'),
    ('av1-opus.mkv', 'matroska', 'libsvtav1', 'libopus'),
    ('flv-mp3.flv', 'flv', 'flv', 'libmp3lame'),
    ('wmv-wma.wmv', 'asf', 'wmv2', 'wmav2'),
    ('prores-pcm.mov', 'mov', 'prores_ks', 'pcm_s16le', {'pix_fmt': 'yuv422p10le'}),
    ('ffv1-flac.mkv', 'matroska', 'ffv1', 'flac'),
    ('huffyuv-pcm.avi', 'avi', 'huffyuv', 'pcm_s16le', {'pix_fmt': 'yuv422p'}),
    ('mjpeg-pcm.avi', 'avi', 'mjpeg', 'pcm_s16le', {'pix_fmt': 'yuvj420p'}),
    ('h264-alac.mov', 'mov', 'libx264', 'alac'),
    ('h264-aac.3gp', '3gp', 'libx264', 'aac'),
    ('rv20.rm', 'rm', 'rv20', None, {'width': 176, 'height': 144}),
    ('silent.mp4', 'mp4', 'libx264', None),
    ('variable-rate.mp4', 'mp4', 'libx264', 'aac', {'vfr': True}),
    ('audio44100.mkv', 'matroska', 'libx264', 'pcm_s16le', {'rate': 44100, 'chirp': True}),
    ('delayed-audio.mkv', 'matroska', 'libx264', 'pcm_s16le', {'offset': .5}),
    ('surround-ac3.mkv', 'matroska', 'libx264', 'ac3', {'layout': '5.1'}),
    ('png-pcm.avi', 'avi', 'png', 'pcm_s16le', {'pix_fmt': 'rgb24', 'width': 163, 'height': 97}),
    ('4k-hevc10.mkv', 'matroska', 'libx265', None,
        {'width': 3840, 'height': 2160, 'fps': 2, 'seconds': 2, 'pix_fmt': 'yuv420p10le'}),
]


def encode(spec):
    name, container, video_codec, audio_codec, *extra = spec
    config = extra[0] if extra else {}
    width, height = config.get('width', 160), config.get('height', 96)
    fps, seconds = config.get('fps', 12), config.get('seconds', 3)
    output = av.open(str(BASE / name), 'w', format=container)
    video = output.add_stream(video_codec, rate=fps)
    video.width = width; video.height = height
    video.pix_fmt = config.get('pix_fmt', 'yuv420p')
    video.codec_context.thread_count = 2
    video.codec_context.gop_size = fps
    if video_codec == 'libx264':
        video.options = {'preset': 'veryfast', 'crf': '18', 'bf': '2', 'sc_threshold': '0'}
    elif video_codec == 'libx265':
        video.options = {'preset': 'ultrafast', 'crf': '22',
            'x265-params': 'log-level=error:pools=none:frame-threads=2:wpp=0:bframes=2:keyint=' + str(fps)}
    elif video_codec.startswith('libvpx'):
        video.options = {'deadline': 'realtime', 'cpu-used': '8', 'crf': '20', 'b': '300k'}
    elif video_codec == 'libsvtav1':
        video.options = {'preset': '12', 'crf': '30', 'svtav1-params': 'lp=2'}
    rate = config.get('rate', 48000); offset = config.get('offset', 0)
    layout = config.get('layout', 'stereo')
    audio = output.add_stream(audio_codec, rate=rate) if audio_codec else None
    if audio:
        audio.layout = layout
        audio.time_base = Fraction(1, rate)
        if audio_codec in ('wmav2', 'aac', 'mp2', 'libmp3lame', 'libopus'): audio.bit_rate = 128000
        elif audio_codec in ('ac3', 'eac3'): audio.bit_rate = 384000 if layout == '5.1' else 192000
        if audio_codec == 'vorbis': audio.options = {'strict': '-2'}
        if audio_codec == 'libopus': audio.options = {'application': 'audio'}
        audio.codec_context.thread_count = 2
    if config.get('vfr'):
        video.codec_context.time_base = Fraction(1, 1000)
        times = [0, .04, .12, .2, .5, .52, .9, 1, 1.7, 1.72, 2.1, 2.6, 2.96]
    else:
        times = [index / fps for index in range(int(seconds * fps))]
    for index, timestamp in enumerate(times):
        pixels = np.empty((height, width, 3), dtype=np.uint8)
        pixels[:] = (32 + index * 5 % 170, 80, 140)
        # A spatial marker catches incorrect plane/stride conversions.
        pixels[:height // 3, :width // 3] = (20, 200, 40)
        frame = av.VideoFrame.from_ndarray(pixels, format='rgb24')
        frame.pts = round(timestamp * 1000) if config.get('vfr') else index + round(config.get('video_offset', 0) * fps)
        frame.time_base = Fraction(1, 1000) if config.get('vfr') else Fraction(1, fps)
        for packet in video.encode(frame): output.mux(packet)
    for packet in video.encode(None): output.mux(packet)
    if audio:
        channels = len(av.AudioLayout(layout).channels)
        t = np.arange(round((seconds - offset) * rate), dtype=np.float64) / rate
        # A changing frequency distinguishes the requested seek time from a
        # different point in a periodic tone that happens to share its phase.
        chirp = 150 * t * t if config.get('chirp') else 0
        signal = np.stack([(np.sin(2 * np.pi * (t * (440 + channel * 110) + chirp)) * 9000).astype(np.int16)
                           for channel in range(channels)])
        block_size = 1152 if audio_codec in ('libmp3lame', 'mp2') else 1024
        for begin in range(0, signal.shape[1], block_size):
            frame = av.AudioFrame.from_ndarray(signal[:, begin:begin + block_size], format='s16p', layout=layout)
            frame.sample_rate = rate; frame.pts = begin + round(offset * rate); frame.time_base = Fraction(1, rate)
            for packet in audio.encode(frame): output.mux(packet)
        for packet in audio.encode(None): output.mux(packet)
    output.close()
    print('Encoded ' + name, flush=True)


def references(spec):
    name = spec[0]
    with av.open(str(BASE / name)) as source:
        origin = (source.start_time or 0) / av.time_base
        stream = source.streams.video[0]
        duration = source.duration / av.time_base if source.duration else float(stream.duration * stream.time_base)
        frames = [(float(frame.pts * frame.time_base) - origin,
                   frame.to_ndarray(format='rgba')[frame.height // 2, frame.width // 2].tolist())
                  for frame in source.decode(video=0)]
        targets = [0, .05, .15, .3, .6, 1.2, 1.9, 2.45, 2.89]
        rows = []
        for time in targets:
            if time >= duration: continue
            chosen = (0, [0, 0, 0, 255]) if frames[0][0] > time + .000001 else frames[0]
            for frame in frames:
                if frame[0] <= time + .000001: chosen = frame
                else: break
            rows.append('\t'.join(map(str, [name, time, chosen[0], *chosen[1]])))
        metadata = '\t'.join(map(str, [name, duration, stream.codec_context.width,
            stream.codec_context.height, int(bool(source.streams.audio))]))
    return metadata, rows


def main():
    BASE.mkdir(parents=True, exist_ok=True)
    stamp = hashlib.sha256((Path(__file__).read_text() + str(av.__version__)).encode()).hexdigest()
    manifest = BASE / 'manifest.json'
    if manifest.exists():
        prior = json.loads(manifest.read_text())
        if prior.get('generator_sha256') == stamp and all((BASE / name).is_file() and
                hashlib.sha256((BASE / name).read_bytes()).hexdigest() == digest for name, digest in prior['files'].items()):
            print('Retained verified encoded video fixtures.'); return
    metadata, frames = [], []
    for spec in SPECS:
        encode(spec)
        info, golden = references(spec); metadata.append(info); frames.extend(golden)
    (BASE / 'fixtures.tsv').write_text('\n'.join(metadata) + '\n')
    (BASE / 'frames.tsv').write_text('\n'.join(frames) + '\n')
    # Concatenate independently resampled PCM; deliberately do not use the
    # container's rounded packet timestamps to join adjacent sample blocks.
    for filename in ('audio44100.mkv', 'h264-aac.mp4'):
        resampler = av.AudioResampler(format='s16', layout='stereo', rate=48000)
        pcm = []
        with av.open(str(BASE / filename)) as source:
            for frame in source.decode(audio=0):
                frame.pts = None
                pcm.extend(output.to_ndarray().tobytes() for output in resampler.resample(frame))
            pcm.extend(output.to_ndarray().tobytes() for output in resampler.resample(None))
        (BASE / (Path(filename).stem + '.s16le')).write_bytes(b''.join(pcm))
    (BASE / 'corrupt.mp4').write_bytes(b'not an mp4 container\x00' * 64)
    (BASE / 'external.mp4').write_text('#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nfile:///etc/passwd\n#EXT-X-ENDLIST\n')
    manifest.write_text(json.dumps({'version': VERSION, 'generator_sha256': stamp,
        'encoder_libraries': av.library_versions, 'hardware_tested': False,
        'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(BASE.iterdir())
                  if p.is_file() and p.name != 'manifest.json'}}, indent=2) + '\n')


if __name__ == '__main__': main()

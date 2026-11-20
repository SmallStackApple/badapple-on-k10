#!/usr/bin/env python3
"""Convert badapple!!.mp4 into the K10 flash media blob.

Format (little-endian):
    char   magic[4]      "BAV1"
    uint16 version       1
    uint16 width
    uint16 height
    uint16 fps
    uint32 frame_count
    uint32 audio_rate
    uint32 audio_samples   (mono int16 frames)
    uint32 frame_data_off
    uint32 audio_off
    uint32 audio_bytes
    uint32 frame_offsets[frame_count]
    ...  frames: uint16 length + RLE bytes, 4-byte aligned ...
    ...  audio: raw s16le mono PCM ...
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import time

WIDTH = 304
HEIGHT = 228
FPS = 30
AUDIO_RATE = 16000
MAGIC = b"BAV1"
HEADER_SIZE = 4 + 2 * 4 + 4 * 6
RUN_RE = re.compile(r"0+|1+")
MAX_RUN = 127


def probe(video: str) -> None:
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=nw=1:nk=1", video],
        check=True, capture_output=True, text=True,
    )
    print(f"source duration: {float(out.stdout.strip()):.3f}s")


def encode_frames(video: str):
    vf = (
        f"fps={FPS},scale={WIDTH}:{HEIGHT}:flags=lanczos,format=gray,"
        "lutyuv=y='if(gte(val,128),235,16)',format=monob"
    )
    proc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", video, "-vf", vf, "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, bufsize=1 << 20,
    )
    frame_bytes = (WIDTH * HEIGHT) // 8
    frames = []
    n = 0
    t0 = time.time()
    while True:
        raw = proc.stdout.read(frame_bytes)
        if len(raw) < frame_bytes:
            break
        bits = format(int.from_bytes(raw, "big"), f"0{WIDTH * HEIGHT}b")
        out = bytearray()
        for m in RUN_RE.finditer(bits):
            run = m.group(0)
            color = 0x80 if run[0] == "1" else 0
            count = len(run)
            while count > 0:
                take = count if count < MAX_RUN else MAX_RUN
                out.append(color | take)
                count -= take
        frames.append(bytes(out))
        n += 1
        if n % 1500 == 0:
            print(f"  encoded {n} frames ({time.time() - t0:.1f}s)", flush=True)
    proc.wait()
    if proc.returncode != 0:
        sys.exit(f"ffmpeg failed with code {proc.returncode}")
    print(f"encoded {n} frames in {time.time() - t0:.1f}s")
    return frames


def encode_audio(video: str, path: str) -> None:
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-i", video, "-vn",
         "-ac", "1", "-ar", str(AUDIO_RATE), "-f", "s16le", path],
        check=True,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("-o", "--output", default="media/badapple.bin")
    ap.add_argument("--preview", help="optional: write one 304x228 PNG preview")
    args = ap.parse_args()

    probe(args.video)
    frames = encode_frames(args.video)
    frame_count = len(frames)

    with tempfile.TemporaryDirectory() as tmp:
        pcm_path = os.path.join(tmp, "audio.pcm")
        encode_audio(args.video, pcm_path)
        audio = open(pcm_path, "rb").read()

    if len(audio) % 2:
        audio += b"\x00"
    audio_samples = len(audio) // 2
    duration_video = frame_count / FPS
    duration_audio = audio_samples / AUDIO_RATE
    print(f"video: {frame_count} frames = {duration_video:.3f}s")
    print(f"audio: {audio_samples} samples = {duration_audio:.3f}s, {len(audio)} bytes")

    frame_data_off = HEADER_SIZE + 4 * frame_count
    offsets = []
    cursor = frame_data_off
    packed = []
    for data in frames:
        cursor = (cursor + 3) & ~3
        offsets.append(cursor)
        entry = struct.pack("<H", len(data)) + data
        packed.append(entry)
        cursor += len(entry)
    audio_off = (cursor + 3) & ~3

    header = struct.pack(
        "<4sHHHHIIIIII",
        MAGIC, 1, WIDTH, HEIGHT, FPS, frame_count,
        AUDIO_RATE, audio_samples, frame_data_off, audio_off, len(audio),
    )
    assert len(header) == HEADER_SIZE

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(header)
        f.write(struct.pack(f"<{frame_count}I", *offsets))
        pos = f.tell()
        for off, entry in zip(offsets, packed):
            f.write(b"\x00" * (off - pos))
            f.write(entry)
            pos = off + len(entry)
        f.write(b"\x00" * (audio_off - pos))
        f.write(audio)

    total = os.path.getsize(args.output)
    video_bytes = sum(len(x) + 2 for x in packed)
    print(f"frame data: {video_bytes} bytes, avg {video_bytes / frame_count:.0f} B/frame")
    print(f"audio offset: 0x{audio_off:08X}")
    print(f"total blob: {total} bytes ({total / (1024*1024):.2f} MiB)")

    if args.preview:
        subprocess.run(
            ["ffmpeg", "-v", "error", "-y", "-i", args.video, "-frames:v", "1",
             "-vf", f"scale={WIDTH}:{HEIGHT}:flags=lanczos,format=gray,"
                    "lutyuv=y='if(gte(val,128),235,16)',format=monob",
             args.preview],
            check=True,
        )
        print(f"preview written: {args.preview}")


if __name__ == "__main__":
    main()

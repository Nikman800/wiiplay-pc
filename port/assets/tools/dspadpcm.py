#!/usr/bin/env python3
"""Decode every Wii Play wave to signed 16-bit PCM.

Covers the BRSTM streams and every wave referenced by the RBNK and RWSD files
embedded in the BRSAR archives (container parsing is reused from fmt_sound).

Ground truth is the nw4r::snd decomp:
  * snd_WaveFile.cpp ReadWaveParam: channel data address is
    waveAddr + WaveInfo.dataOffset + WaveChannelInfo.channelDataOffset, where
    waveAddr = GroupInfo.waveDataOffset + GroupItemInfo.waveDataOffset (both
    absolute in the RSAR). WaveChannelInfo.adpcmOffset is relative to the
    WaveInfo itself. loopStart/loopEnd are DSP addresses; the playable length
    is GetSampleByDspAddress(loopEnd) + 1 samples (snd_AxVoice.cpp).
  * snd_StrmPlayer.cpp: stream block b of channel c starts at
    dataOffset + b * (blockHeaderOffset + blockSize * numChannels)
    + blockHeaderOffset + c * RoundUp(size, 32), where size is blockSize, or
    lastBlockPaddedSize for the last block. The blocks are copied into one
    contiguous ring buffer per channel and played as a single voice, so the
    DSP-ADPCM history carries across block boundaries.
  * DSP-ADPCM: 8-byte frames of a predictor/scale header byte followed by
    14 4-bit samples, high nibble first.

Usage:
  dspadpcm.py --check ROOT        decode everything and print a summary
  dspadpcm.py --dump DIR ROOT     write RIFF PCM16 WAVs (DIR outside the repo)
"""

from __future__ import annotations

import argparse
import array
import collections
from pathlib import Path
import struct
import sys
from typing import Any, Iterator

try:
    from .fmt_sound import FormatError, parse
    from .wiiarc import walk
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from fmt_sound import FormatError, parse
    from wiiarc import walk


PCM8 = 0
PCM16 = 1
DSP_ADPCM = 2
CODEC_NAMES = {PCM8: "PCM8", PCM16: "PCM16", DSP_ADPCM: "DSP-ADPCM"}

SAMPLES_PER_FRAME = 14
NIBBLES_PER_FRAME = 16
FRAME_SIZE = 8


def _s16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _need_data(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or len(data) < offset + size:
        raise FormatError(f"{what} at 0x{offset:x} ({size} bytes) is out of bounds")


def _round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def sample_by_dsp_address(address: int, codec: int) -> int:
    """AxVoice::GetSampleByDspAddress with a zero base."""
    if codec == DSP_ADPCM:
        return (address % NIBBLES_PER_FRAME) + address // NIBBLES_PER_FRAME * SAMPLES_PER_FRAME - 2
    return address


def codec_bytes(sample_count: int, codec: int) -> int:
    """Bytes needed to hold sample_count samples of one channel."""
    if codec == DSP_ADPCM:
        return (sample_count + SAMPLES_PER_FRAME - 1) // SAMPLES_PER_FRAME * FRAME_SIZE
    if codec == PCM16:
        return sample_count * 2
    return sample_count


def decode_dsp_adpcm(
    data: bytes,
    offset: int,
    sample_count: int,
    coefficients: list[int] | tuple[int, ...],
    yn1: int,
    yn2: int,
) -> tuple[array.array, int, int]:
    """Decode sample_count samples starting at data[offset].

    Returns (samples, yn1, yn2) so a caller can continue with the next block.
    yn1/yn2 are signed history values (most recent first).
    """
    if len(coefficients) != 16:
        raise FormatError(f"DSP-ADPCM needs 8 coefficient pairs, got {len(coefficients)} values")
    coefs = [_s16(c & 0xFFFF) for c in coefficients]
    frame_count = (sample_count + SAMPLES_PER_FRAME - 1) // SAMPLES_PER_FRAME
    _need_data(data, offset, frame_count * FRAME_SIZE, "DSP-ADPCM frames")
    out = array.array("h", bytes(2 * sample_count))
    index = 0
    position = offset
    for _frame in range(frame_count):
        header = data[position]
        predictor = header >> 4
        if predictor > 7:
            raise FormatError(f"DSP-ADPCM predictor index {predictor} at 0x{position:x} is invalid")
        scale = 1 << (header & 0x0F)
        c1 = coefs[predictor * 2]
        c2 = coefs[predictor * 2 + 1]
        take = sample_count - index
        if take > SAMPLES_PER_FRAME:
            take = SAMPLES_PER_FRAME
        for n in range(take):
            byte = data[position + 1 + (n >> 1)]
            nibble = (byte & 0x0F) if n & 1 else (byte >> 4)
            if nibble >= 8:
                nibble -= 16
            sample = (c1 * yn1 + c2 * yn2 + ((nibble * scale) << 11) + 1024) >> 11
            if sample > 32767:
                sample = 32767
            elif sample < -32768:
                sample = -32768
            out[index] = sample
            index += 1
            yn2 = yn1
            yn1 = sample
        position += FRAME_SIZE
    return out, yn1, yn2


def decode_pcm(data: bytes, offset: int, sample_count: int, codec: int) -> array.array:
    if codec == PCM16:
        _need_data(data, offset, sample_count * 2, "PCM16 samples")
        out = array.array("h", data[offset:offset + sample_count * 2])
        if sys.byteorder != "big":
            out.byteswap()
        return out
    if codec == PCM8:
        _need_data(data, offset, sample_count, "PCM8 samples")
        return array.array("h", ((b - 256 if b & 0x80 else b) << 8 for b in data[offset:offset + sample_count]))
    raise FormatError(f"unsupported codec {codec}")


def _adpcm_info(blob: bytes, offset: int, what: str) -> dict[str, Any]:
    """AdpcmInfo (snd_Types.h): coef[16], gain, pred_scale, yn1, yn2, loop_pred_scale, loop_yn1, loop_yn2."""
    _need_data(blob, offset, 0x30, what)
    coefficients = struct.unpack(">16h", blob[offset:offset + 0x20])
    gain, pred_scale, yn1, yn2, loop_ps, loop_yn1, loop_yn2 = struct.unpack(">7H", blob[offset + 0x20:offset + 0x2E])
    return {
        "coefficients": coefficients,
        "gain": gain,
        "predScale": pred_scale,
        "yn1": _s16(yn1),
        "yn2": _s16(yn2),
        "loopPredScale": loop_ps,
        "loopYn1": _s16(loop_yn1),
        "loopYn2": _s16(loop_yn2),
    }


def _new_diag() -> dict[str, int]:
    return {"psMismatch": 0, "loopPsMismatch": 0, "loopHistMismatch": 0, "loopHistChecks": 0,
            "adpcEntries": 0, "adpcExact": 0, "adpcAbsDiff": 0, "adpcAbsAmp": 0}


# ---------------------------------------------------------------- BRSTM


def decode_brstm(data: bytes, name: str = "stream") -> dict[str, Any]:
    parsed = parse(data)
    if parsed["format"] != "RSTM":
        raise FormatError("not an RSTM stream")
    info = parsed["headBlock"]["strmDataInfo"]
    channel_infos = parsed["headBlock"]["channels"]
    codec = info["format"]
    channel_count = info["numChannels"]
    blocks = info["numBlocks"]
    total = (blocks - 1) * info["blockSamples"] + info["lastBlockSamples"]
    if total <= 0:
        raise FormatError("RSTM has no samples")
    if info["loopEnd"] > total:
        raise FormatError(f"RSTM loopEnd {info['loopEnd']} exceeds {total} samples")
    stride = info["blockHeaderOffset"] + info["blockSize"] * channel_count
    adpc_offset = parsed["header"]["adpcBlockOffset"] if parsed["adpcBlock"] else None
    diag = _new_diag()

    def block_start(b: int, ch: int) -> int:
        size = info["lastBlockPaddedSize"] if b == blocks - 1 else info["blockSize"]
        return info["dataOffset"] + b * stride + info["blockHeaderOffset"] + ch * _round_up(size, 32)

    channels = []
    for ch in range(channel_count):
        out = array.array("h")
        if codec == DSP_ADPCM:
            adpcm = channel_infos[ch]
            coefs = adpcm["coefficients"]
            yn1, yn2 = _s16(adpcm["yn1"]), _s16(adpcm["yn2"])
        for b in range(blocks):
            last = b == blocks - 1
            size = info["lastBlockPaddedSize"] if last else info["blockSize"]
            count = info["lastBlockSamples"] if last else info["blockSamples"]
            if codec_bytes(count, codec) > size:
                raise FormatError(f"RSTM block {b} holds fewer bytes than its samples need")
            start = block_start(b, ch)
            if codec == DSP_ADPCM:
                if b == 0 and data[start] != adpcm["predScale"]:
                    diag["psMismatch"] += 1
                block, yn1, yn2 = decode_dsp_adpcm(data, start, count, coefs, yn1, yn2)
            else:
                block = decode_pcm(data, start, count, codec)
            out.extend(block)
        channels.append(out)
    if codec == DSP_ADPCM and adpc_offset is not None and info["adpcmDataSize"] == 4 and info["adpcmDataInterval"]:
        # ADPC block: (yn1, yn2) per channel every adpcmDataInterval samples, used by the
        # runtime only for start-offset seeking. On this disc the encoder wrote the history of
        # its *source* PCM (and sized the table by bytes, not samples), so it only approximates
        # the decoded history; record the distance as a sanity check.
        interval = info["adpcmDataInterval"]
        entries = (parsed["header"]["adpcBlockSize"] - 8) // (4 * channel_count)
        for k in range(1, entries):
            i = k * interval
            if i >= total:
                break
            for ch in range(channel_count):
                entry = adpc_offset + 8 + (k * channel_count + ch) * 4
                h1 = struct.unpack(">h", data[entry:entry + 2])[0]
                got = channels[ch][i - 1]
                diag["adpcEntries"] += 1
                diag["adpcExact"] += h1 == got
                diag["adpcAbsDiff"] += abs(h1 - got)
                diag["adpcAbsAmp"] += abs(got)
    if codec == DSP_ADPCM and info["loopFlag"]:
        # The loop context must equal the header of the frame holding loopStart.
        b, rest = divmod(info["loopStart"], info["blockSamples"])
        for ch in range(channel_count):
            pos = block_start(b, ch) + rest // SAMPLES_PER_FRAME * FRAME_SIZE
            if data[pos] != channel_infos[ch]["loopPredScale"]:
                diag["loopPsMismatch"] += 1
            ls = info["loopStart"]
            if ls >= 2:
                diag["loopHistChecks"] += 1
                expected = (_s16(channel_infos[ch]["loopYn1"]), _s16(channel_infos[ch]["loopYn2"]))
                if (channels[ch][ls - 1], channels[ch][ls - 2]) != expected:
                    diag["loopHistMismatch"] += 1
    return {
        "kind": "BRSTM",
        "name": name,
        "sampleRate": info["sampleRate"],
        "channels": channel_count,
        "codec": codec,
        "frames": total,
        "loop": bool(info["loopFlag"]),
        "loopStart": info["loopStart"],
        "loopEnd": info["loopEnd"],
        "channelSamples": channels,
        "blockSamples": info["blockSamples"],
        "diag": diag,
    }


# ---------------------------------------------------------------- BRSAR


def _wave_info_offsets(blob: bytes, kind: str) -> list[int]:
    """Offsets of each WaveInfo inside an embedded RBNK/RWSD (same walk as fmt_sound)."""
    wave_block = struct.unpack(">I", blob[0x18:0x1C])[0]
    count = struct.unpack(">I", blob[wave_block + 8:wave_block + 12])[0]
    offsets = []
    for i in range(count):
        if kind == "RBNK":
            entry = wave_block + 12 + i * 8
            offsets.append(wave_block + 8 + struct.unpack(">I", blob[entry + 4:entry + 8])[0])
        else:
            entry = wave_block + 12 + i * 4
            offsets.append(wave_block + struct.unpack(">I", blob[entry:entry + 4])[0])
    return offsets


def _decode_bank_wave(
    rsar: bytes,
    blob: bytes,
    info_offset: int,
    wave: dict[str, Any],
    wave_base: int,
    wave_region_end: int,
    kind: str,
    name: str,
) -> dict[str, Any]:
    codec = wave["format"]
    count = sample_by_dsp_address(wave["loopEnd"], codec) + 1
    loop_start = sample_by_dsp_address(wave["loopStart"], codec)
    if count <= 0:
        raise FormatError(f"{name}: wave has no samples")
    if wave["loopFlag"] and not 0 <= loop_start < count:
        raise FormatError(f"{name}: loop start {loop_start} outside 0..{count}")
    need = codec_bytes(count, codec)
    diag = _new_diag()
    channels = []
    for ch, channel in enumerate(wave["channels"]):
        what = f"{name} channel {ch}"
        start = wave_base + wave["dataOffset"] + channel["channelDataOffset"]
        if start + need > wave_region_end:
            raise FormatError(f"{what}: {need} data bytes run past the group item wave region")
        if codec == DSP_ADPCM:
            if not channel["adpcmOffset"]:
                raise FormatError(f"{what}: DSP-ADPCM channel has no AdpcmInfo")
            adpcm = _adpcm_info(blob, info_offset + channel["adpcmOffset"], what)
            if rsar[start] != adpcm["predScale"]:
                diag["psMismatch"] += 1
            if wave["loopFlag"]:
                pos = start + loop_start // SAMPLES_PER_FRAME * FRAME_SIZE
                if rsar[pos] != adpcm["loopPredScale"]:
                    diag["loopPsMismatch"] += 1
            samples, _, _ = decode_dsp_adpcm(rsar, start, count, adpcm["coefficients"], adpcm["yn1"], adpcm["yn2"])
            if wave["loopFlag"] and loop_start >= 2:
                diag["loopHistChecks"] += 1
                if (samples[loop_start - 1], samples[loop_start - 2]) != (adpcm["loopYn1"], adpcm["loopYn2"]):
                    diag["loopHistMismatch"] += 1
        else:
            samples = decode_pcm(rsar, start, count, codec)
        channels.append(samples)
    return {
        "kind": kind,
        "name": name,
        "sampleRate": wave["sampleRate"],
        "channels": wave["numChannels"],
        "codec": codec,
        "frames": count,
        "loop": bool(wave["loopFlag"]),
        "loopStart": loop_start,
        "loopEnd": count,
        "channelSamples": channels,
        "diag": diag,
    }


def iter_brsar(data: bytes, errors: list[str], path: str) -> Iterator[dict[str, Any]]:
    """Yield each RBNK/RWSD wave once (a file placed in several groups is decoded once)."""
    parsed = parse(data)
    groups = parsed["infoBlock"]["groups"]
    seen_files: set[int] = set()
    for embedded in parsed["embedded"]:
        sub = embedded["parsed"]
        if sub is None or sub["format"] not in ("RBNK", "RWSD"):
            continue
        if embedded["fileId"] in seen_files:
            continue
        seen_files.add(embedded["fileId"])
        kind = sub["format"]
        group = groups[embedded["groupId"]]
        item = group["items"][embedded["index"]]
        blob_offset = group["offset"] + item["offset"]
        blob = data[blob_offset:blob_offset + item["size"]]
        wave_base = group["waveDataOffset"] + item["waveDataOffset"]
        wave_end = wave_base + item["waveDataSize"]
        waves = sub["waveBlock"]["items"] if kind == "RBNK" else sub["waveBlock"]["waves"]
        offsets = _wave_info_offsets(blob, kind)
        if len(offsets) != len(waves):
            errors.append(f"{path} file {embedded['fileId']}: wave table count disagrees with fmt_sound")
            continue
        for index, (wave, info_offset) in enumerate(zip(waves, offsets)):
            name = f"{kind}-file{embedded['fileId']}-wave{index}"
            try:
                yield _decode_bank_wave(data, blob, info_offset, wave, wave_base, wave_end, kind, name)
            except FormatError as error:
                errors.append(f"{path} {name}: {error}")


def iter_decoded(root: str, errors: list[str]) -> Iterator[tuple[str, dict[str, Any]]]:
    for path, blob in walk(root):
        magic = blob[:4]
        if magic == b"RSTM":
            try:
                yield path, decode_brstm(blob, Path(path).stem)
            except FormatError as error:
                errors.append(f"{path}: {error}")
        elif magic == b"RSAR":
            try:
                for wave in iter_brsar(blob, errors, path):
                    yield path, wave
            except FormatError as error:
                errors.append(f"{path}: {error}")


# ---------------------------------------------------------------- check


def _clipped(samples: array.array) -> int:
    return samples.count(32767) + samples.count(-32768)


def check(root: str) -> tuple[list[str], int]:
    errors: list[str] = []
    containers: collections.Counter[str] = collections.Counter()
    codecs: collections.Counter[tuple[str, str]] = collections.Counter()
    channels_c: collections.Counter[tuple[str, int]] = collections.Counter()
    rates: collections.Counter[tuple[str, int]] = collections.Counter()
    loops: collections.Counter[str] = collections.Counter()
    frames: collections.Counter[str] = collections.Counter()
    seconds: collections.defaultdict[str, float] = collections.defaultdict(float)
    diag_total: collections.Counter[str] = collections.Counter()
    sources: set[str] = set()
    samples_total = 0
    clipped_total = 0
    flagged = []
    longest = (0.0, "")
    for path, wave in iter_decoded(root, errors):
        kind = wave["kind"]
        sources.add(path)
        containers[kind] += 1
        codecs[(kind, CODEC_NAMES[wave["codec"]])] += 1
        channels_c[(kind, wave["channels"])] += 1
        rates[(kind, wave["sampleRate"])] += 1
        loops[f"{kind}:{'loop' if wave['loop'] else 'oneshot'}"] += 1
        frames[kind] += wave["frames"]
        duration = wave["frames"] / wave["sampleRate"]
        seconds[kind] += duration
        if duration > longest[0]:
            longest = (duration, f"{path} {wave['name']}")
        for key, value in wave["diag"].items():
            diag_total[key] += value
        n = sum(len(c) for c in wave["channelSamples"])
        c = sum(_clipped(ch) for ch in wave["channelSamples"])
        samples_total += n
        clipped_total += c
        if n and c / n > 0.01:
            flagged.append((c / n, path, f"{wave['name']} {CODEC_NAMES[wave['codec']]}"))

    def fmt(counter: collections.Counter[Any]) -> str:
        return ", ".join(
            f"{'/'.join(map(str, k)) if isinstance(k, tuple) else k}x{v}" for k, v in sorted(counter.items())
        )

    lines = [
        f"sources={len(sources)} decoded={sum(containers.values())} errors={len(errors)}",
        f"waves: {fmt(containers)}",
        f"codecs: {fmt(codecs)}",
        f"channels: {fmt(channels_c)}",
        f"sampleRates: {fmt(rates)}",
        f"loops: {fmt(loops)}",
        "sampleFrames (per channel): " + ", ".join(f"{k}={v}" for k, v in sorted(frames.items())),
        "seconds: " + ", ".join(f"{k}={seconds[k]:.1f}" for k in sorted(seconds)),
        f"longest: {longest[0]:.2f}s {longest[1]}",
        "DSP-ADPCM consistency: "
        f"first-frame header != AdpcmInfo.pred_scale: {diag_total['psMismatch']}; "
        f"loop frame header != loop_pred_scale: {diag_total['loopPsMismatch']}; "
        f"decoded history before loop start != AdpcmInfo.loop_yn1/yn2: "
        f"{diag_total['loopHistMismatch']}/{diag_total['loopHistChecks']}",
        f"BRSTM ADPC seek history vs decoded yn1: {diag_total['adpcEntries']} entries, "
        f"{diag_total['adpcExact']} exact, mean |diff| "
        f"{diag_total['adpcAbsDiff'] / max(1, diag_total['adpcEntries']):.0f} vs mean |sample| "
        f"{diag_total['adpcAbsAmp'] / max(1, diag_total['adpcEntries']):.0f} (encoder source history; approximate by design)",
        f"clippedSamples={clipped_total}/{samples_total} ({clipped_total / samples_total if samples_total else 0:.4%})",
        f"waves clipping >1%: {len(flagged)}",
    ]
    for fraction, path, name in sorted(flagged, reverse=True):
        lines.append(f"  CLIP {fraction:.2%} {path} {name}")
    for error in errors:
        lines.append(f"ERROR {error}")
    return lines, len(errors)


# ---------------------------------------------------------------- WAV


def _repository_root() -> Path:
    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / ".git").exists():
            return parent
    return here.parents[3]


def _safe_dump_dir(dump_dir: Path) -> Path:
    resolved = dump_dir.resolve()
    repository = _repository_root()
    if resolved == repository or repository in resolved.parents:
        raise SystemExit(f"refusing --dump DIR inside the repository ({resolved})")
    return resolved


def wav_bytes(wave: dict[str, Any]) -> bytes:
    channels = wave["channelSamples"]
    count = len(channels)
    if count == 1:
        pcm = array.array("h", channels[0])
    else:
        pcm = array.array("h", bytes(2 * wave["frames"] * count))
        for index, samples in enumerate(channels):
            pcm[index::count] = samples
    if sys.byteorder == "big":
        pcm.byteswap()
    payload = pcm.tobytes()
    rate = wave["sampleRate"]
    fmt_chunk = b"fmt " + struct.pack("<IHHIIHH", 16, 1, count, rate, rate * count * 2, count * 2, 16)
    data_chunk = b"data" + struct.pack("<I", len(payload)) + payload
    return b"RIFF" + struct.pack("<I", 4 + len(fmt_chunk) + len(data_chunk)) + b"WAVE" + fmt_chunk + data_chunk


def dump(root: str, dump_dir: Path) -> tuple[int, list[str]]:
    output = _safe_dump_dir(dump_dir)
    output.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    written = 0
    for path, wave in iter_decoded(root, errors):
        stem = path.replace("/", "_").removesuffix(".brstm").removesuffix(".brsar")
        filename = stem + ".wav" if wave["kind"] == "BRSTM" else f"{stem}__{wave['name']}.wav"
        (output / filename).write_bytes(wav_bytes(wave))
        written += 1
    return written, errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", metavar="ROOT", help="decode all waves under ROOT and print a summary")
    parser.add_argument("--dump", metavar="DIR", help="write RIFF PCM16 WAVs to DIR (outside the repository)")
    parser.add_argument("root", nargs="?", help="disc files root for --dump")
    args = parser.parse_args(argv)
    if args.check:
        lines, error_count = check(args.check)
        print("\n".join(lines))
        return 1 if error_count else 0
    if args.dump:
        if not args.root:
            parser.error("--dump requires ROOT")
        written, errors = dump(args.root, Path(args.dump))
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        print(f"wrote {written} WAV files to {args.dump}, errors={len(errors)}")
        return 1 if errors else 0
    parser.error("choose --check ROOT or --dump DIR ROOT")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

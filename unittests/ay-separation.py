#!/usr/bin/env python3
"""Validate AY routing with an automation-enabled null-UI/null-audio Fuse.

Run from the source root: python3 unittests/ay-separation.py [./fuse]
Generated 128K snapshots sound one AY channel at a time; no game files needed.
"""
import array
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave


def snapshot(path, channel):
    code = bytearray([0xf3])  # DI
    for register, value in [(0, 128), (1, 0), (2, 128), (3, 0),
                            (4, 128), (5, 0), (7, 0x38),
                            (8, 15 if channel == 0 else 0),
                            (9, 15 if channel == 1 else 0),
                            (10, 15 if channel == 2 else 0)]:
        code += bytes.fromhex('01 fd ff 3e') + bytes([register])
        code += bytes.fromhex('ed 79 01 fd bf 3e') + bytes([value])
        code += bytes.fromhex('ed 79')
    code += b'\xc3' + struct.pack('<H', 0x8000 + len(code))
    header = bytearray(30)
    header[8:10] = struct.pack('<H', 0x9000)
    header[29] = 1
    extra = bytearray(23)
    extra[:2] = struct.pack('<H', 0x8000)
    extra[2] = 3  # Z80 v2 Spectrum 128K
    pages = bytearray()
    for bank in range(8):
        page = bytearray(16384)
        if bank == 2:
            page[:len(code)] = code
        pages += struct.pack('<HB', 0xffff, bank + 3) + page
    path.write_bytes(header + struct.pack('<H', 23) + extra + pages)


def main():
    executable = str(Path(sys.argv[1] if len(sys.argv) > 1 else './fuse').resolve())
    with tempfile.TemporaryDirectory(prefix='fuse-ay-separation-') as temporary:
        root = Path(temporary)
        for mode in ('None', 'ACB', 'ABC'):
            for channel in range(3):
                fixture = root / f'{channel}.z80'
                snapshot(fixture, channel)
                output = root / f'{mode}-{channel}'
                output.mkdir()
                subprocess.run([
                    executable, '--machine', '128', '--snapshot', str(fixture),
                    '--sound', '--speaker-type', 'Unfiltered', '--separation', mode,
                    '--automation-frames', '30', '--automation-capture-audio',
                    '--automation-output', str(output),
                ], check=True, stdout=subprocess.DEVNULL)
                result = json.loads((output / 'result.json').read_text())
                audio = result['settings']['audio']
                assert audio['ay_channel_arrangement'] == mode.lower(), audio
                with wave.open(str(output / 'audio.wav')) as wav:
                    channels = wav.getnchannels()
                    assert channels == (1 if mode == 'None' else 2)
                    samples = array.array('h', wav.readframes(wav.getnframes()))
                if sys.byteorder != 'little':
                    samples.byteswap()
                # Discard setup and filter startup; examine steady-state tones.
                energy = [sum(v * v for v in samples[4410 * channels + i::channels])
                          for i in range(channels)]
                middle = 2 if mode == 'ACB' else 1
                expected = ([True] if channels == 1 else
                            [channel in (0, middle), channel != 0])
                assert max(energy) > 1_000_000, (mode, channel, energy)
                # Final-mix silence includes low-level decorrelated noise.
                for actual, active in zip(energy, expected):
                    assert (actual > max(energy) * 0.00001) == active, (
                        mode, channel, energy)
                if channels == 2 and channel == middle:
                    assert abs(energy[0] - energy[1]) < max(energy) * 0.001, (
                        mode, energy)
                print(f'{mode:4} channel {"ABC"[channel]}: PASS (energy {energy})')


if __name__ == '__main__':
    main()

"""Offline integration tests: no audio playback and no extra Python packages."""
import math
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'dist' / 'tonelab-cli.exe'


def run(*args, code=0):
    result = subprocess.run([str(EXE), *args], capture_output=True, text=True)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result


def main():
    run('--help')
    for args in [('--rate', 'nan'), ('--rate', '384000bad'), ('--format', '64'),
                 ('--left-freq', '192000'), ('--left-amp', '1.1'),
                 ('--right-mute', 'yes'), ('--unknown', '1')]:
        run('render', '--output', 'unused.wav', '--seconds', '1', *args, code=2)
    with tempfile.TemporaryDirectory() as folder:
        for fmt, size, tag in [('16', 2, 1), ('24', 3, 1), ('32', 4, 1), ('f32', 4, 3)]:
            path = Path(folder) / f'native-{fmt}.wav'
            run('render', '--output', str(path), '--seconds', '0.1', '--rate', '384000',
                '--format', fmt, '--left-freq', '1000', '--right-freq', '2000')
            blob = path.read_bytes()
            assert blob[:4] == b'RIFF' and blob[8:12] == b'WAVE'
            assert struct.unpack_from('<I', blob, 4)[0] == len(blob) - 8
            assert struct.unpack_from('<HHIIHH', blob, 20) == (tag, 2, 384000, 384000*2*size, 2*size, size*8)
            position = 12
            while blob[position:position+4] != b'data':
                length = struct.unpack_from('<I', blob, position+4)[0]
                position += 8 + length + length % 2
            data = blob[position+8:]
            assert len(data) == 38400*2*size
            for frame in range(4000, 38400, 137):
                for channel, frequency in enumerate((1000, 2000)):
                    at = (frame*2+channel)*size
                    sample = (struct.unpack_from('<f', data, at)[0] if tag == 3 else
                              int.from_bytes(data[at:at+size], 'little', signed=True) / 2**(size*8-1))
                    expected = 0.1*math.sin(2*math.pi*frequency*frame/384000)
                    assert abs(sample-expected) < (2/2**(size*8-1) if tag == 1 else 1e-7)
            if fmt == '32':
                assert any(data[i] & 127 for i in range(0, len(data), 4))
        run('render', '--output', str(Path(folder)/'missing'/'file.wav'), '--seconds', '1', code=1)
        for fmt in ('16', '24', '32', 'f32'):
            path = Path(folder) / f'zero-{fmt}.wav'
            run('render', '--output', str(path), '--seconds', '0.02', '--rate', '384000',
                '--format', fmt, '--left-mute', '1', '--right-mute', '1')
            blob = path.read_bytes()
            position = blob.index(b'data', 36)
            assert not any(blob[position+8:]), 'Muted channels must contain only zero bytes'
        path = Path(folder) / '24in32.wav'
        run('render', '--output', str(path), '--seconds', '0.02', '--rate', '384000', '--format', '24in32')
        blob = path.read_bytes()
        assert struct.unpack_from('<H', blob, 20)[0] == 65534
        assert struct.unpack_from('<H', blob, 38)[0] == 24
        assert len(blob) == 68 + 7680*8
        assert all(blob[i] == 0 for i in range(68, len(blob), 4))
    print('PASS: CLI arguments, exit codes, WAV headers, 384 kHz PCM16/24/32/f32 samples, PCM32 low bits.')


if __name__ == '__main__':
    main()

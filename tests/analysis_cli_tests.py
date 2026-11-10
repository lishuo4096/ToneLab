"""No hardware required: CLI, float64 precision, silence and invalid input checks."""
from pathlib import Path
import json
import math
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'dist' / 'tonescope-cli.exe'


def run(*args, success=True):
    result = subprocess.run([str(EXE), *args], capture_output=True)
    assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)


run('--help')
with tempfile.TemporaryDirectory() as folder:
    base = Path(folder)
    wav, report = base / 'deep.wav', base / 'result.json'
    # IEEE float64 file with a -140 dBc H2, not quantized through float32.
    n, fs, f0, amplitude = 32768, 48000, 997.137, 0.25
    data = bytearray()
    for i in range(n):
        t = i / fs
        sample = amplitude*math.sin(2*math.pi*f0*t) + amplitude*1e-7*math.sin(4*math.pi*f0*t)
        data.extend(struct.pack('<dd', sample, 0.0))
    header = b'RIFF'+struct.pack('<I', 36+len(data))+b'WAVEfmt '+struct.pack('<IHHIIHH', 16, 3, 2, fs, fs*16, 16, 64)+b'data'+struct.pack('<I', len(data))
    wav.write_bytes(header+data)
    run('analyze', '--input', str(wav), '--fft', str(n), '--json', str(report), '--csv', str(base/'spectrum.csv'))
    result = json.loads(report.read_text())
    assert result['channels'][0]['valid']
    assert abs(result['channels'][0]['thd_db']+140) < 0.05, result
    assert not result['channels'][1]['valid'], 'Silent channel must not report valid distortion'
    for option in [('--fft', '12345'), ('--fft', 'nan'), ('--low', '-1'), ('--high', '0'), ('--harmonics', '99'), ('--start', '1e200'), ('--duration', '-1'), ('--shared', 'wrong'), ('--unknown', '1')]:
        run('analyze', '--input', str(wav), *option, success=False)
    bad = base/'broken.wav'
    bad.write_bytes(b'not a wave file')
    run('analyze', '--input', str(bad), success=False)
print('PASS: float64 -140 dBc THD, silence rejection, WAV/CSV/JSON and invalid parameter handling.')

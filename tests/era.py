#!/usr/bin/env python3
"""DM-era XML fixtures; full digest bodies and independently replayed deltas are oracles.
No device-mapper mappings or existing devices are touched.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile

binary = str(pathlib.Path('src/blocksync-fast').resolve())
probe = str(pathlib.Path('.test-build/io-probe.so').resolve())
calls = 0

with tempfile.TemporaryDirectory(prefix='bsf-era-') as directory:
    root = pathlib.Path(directory)
    source, digest, xml, delta, target, full = [root / n for n in
                                              ('source', 'digest', 'era.xml', 'delta', 'target', 'full')]

    def run(args, ok=True, data=None, env=None):
        global calls
        calls += 1
        result = subprocess.run([binary, *args], input=data, env=env,
                                capture_output=True, timeout=30)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def snapshot(path):
        return (path.read_bytes(), path.stat().st_mtime_ns) if path.exists() else None

    def reset(size, mode=(), block=4096):
        for p in root.iterdir():
            p.unlink()
        source.write_bytes((bytes(range(256)) * ((size + 255) // 256))[:size])
        shutil.copyfile(source, target)
        run(['--make-digest', '-s', str(source), '-f', str(digest), '-b', str(block), *mode])

    def era(mode=(), sectors=8, block=4096, extra=()):
        return ['--make-digest', '-s', str(source), '-f', str(digest), '-e', str(xml),
                '-E', str(sectors), '-b', str(block), *mode, *extra]

    def full_body(block=4096):
        full.unlink(missing_ok=True)
        run(['--make-digest', '-s', str(source), '-f', str(full), '-b', str(block)])
        return full.read_bytes()[512:]

    def replay_independently():
        data = delta.read_bytes()
        position = 512
        result = bytearray(target.read_bytes())
        records = []
        while position < len(data):
            offset, = struct.unpack_from('Q', data, position)
            position += 8
            length = min(4096, len(result) - offset)
            assert length > 0 and position + length <= len(data)
            result[offset:offset + length] = data[position:position + length]
            position += length
            records.append(offset)
        assert position == len(data)
        assert bytes(result) == source.read_bytes()
        return records

    for mode in ([], ['--mmap']):
        # Gaps, overlapping/unsorted ranges, digest buffer boundaries and a partial tail.
        reset(5 * 1024 * 1024 + 7, mode)
        for offset in (0, 4096, 3 * 1024 * 1024, source.stat().st_size - 1):
            with source.open('r+b') as f:
                f.seek(offset)
                f.write(b'Z')
        last = (source.stat().st_size - 1) // 4096
        xml.write_text(f'''<?xml version="1.0"?>
<!-- unordered ranges are normalized -->
<blocks><block block='{last}'/><range end="770" begin="768"/>
<range begin="0" end="2"/><block block="1"/></blocks>''')
        baseline = digest.read_bytes()
        old = snapshot(digest)
        run(era(mode, extra=['--dont-write']))
        assert snapshot(digest) == old and not (root / 'digest.incomplete').exists()
        run(era(mode))
        assert digest.read_bytes()[512:] == full_body()
        # Generate delta from the old baseline, then replay both independently and via the CLI.
        digest.write_bytes(baseline)
        run([*era(mode), '--make-delta', '-D', str(delta)])
        records = replay_independently()
        assert records == [0, 4096, 3 * 1024 * 1024, last * 4096], records
        assert digest.read_bytes()[512:] == full_body()
        run(['--apply-delta', '-d', str(target), '-D', str(delta), *mode])
        subprocess.run(['cmp', '--', str(source), str(target)], check=True)
        assert not (root / 'digest.incomplete').exists()

        # Empty lists retain all entries, compact/CRLF XML and stdin XML are accepted.
        for text in ('<blocks/>', '<blocks>\r\n</blocks>\r\n', '<blocks><!-- none --></blocks>'):
            xml.write_text(text)
            old_body = digest.read_bytes()[512:]
            run(era(mode))
            assert digest.read_bytes()[512:] == old_body
        run([*era(mode), '-e', '-'], data=b'<blocks><block block="0"/></blocks>')

        # Era blocks finer/coarser than hash blocks expand to all intersecting hash blocks.
        for sectors, block, changed in ((1, 4096, 600), (16, 4096, 5000), (8, 8192, 5000)):
            reset(16385, mode, block)
            with source.open('r+b') as f:
                f.seek(changed)
                f.write(b'Q')
            index = changed // (sectors * 512)
            xml.write_text(f'<blocks><block block="{index}"/></blocks>')
            run(era(mode, sectors, block))
            assert digest.read_bytes()[512:] == full_body(block)
        print('PASS era digest / delta replay / mmap / gaps / partial tails / stdin / block sizes', mode,
              flush=True)

    reset(4097)
    malformed = ('', '<blocks>', '<blocks><block block="0"/>', '<blocks></blocks>junk',
                 '<blocks><block block="-1"/></blocks>', '<blocks><block block="0junk"/></blocks>',
                 '<blocks><block block="18446744073709551615"/></blocks>',
                 '<blocks><range begin="2" end="1"/></blocks>',
                 '<blocks><range begin="0" end="0"/></blocks>',
                 '<blocks><range begin="0"/></blocks>',
                 '<blocks><block block="0" block="1"/></blocks>',
                 '<blocks><block block="2"/></blocks>',
                 '<blocks><range begin="0" end="3"/></blocks>',
                 '<!DOCTYPE blocks><blocks/>', '<blocks>\x00</blocks>',
                 '<blocks><block block="0" unknown="1"/></blocks>',
                 '<blocks><block block="0"end="1"/></blocks>')
    delta.write_bytes(b'existing output')
    for text in malformed:
        xml.write_text(text)
        before = [snapshot(p) for p in (source, digest, xml, delta)]
        run([*era(), '--make-delta', '-D', str(delta), '--force'], ok=False)
        assert before == [snapshot(p) for p in (source, digest, xml, delta)]
        assert not (root / 'digest.incomplete').exists()
    xml.write_text('<blocks><block block="0"/></blocks>')
    for count in ('0', '-1', 'junk', '8K', '18446744073709551616'):
        old = snapshot(digest)
        run([*era(), '-E', count], ok=False)
        assert snapshot(digest) == old
    for extra in (['--no-compare'], ['--make-delta', '--dont-write'], ['--read-jobs=1'],
                  ['--apply-delta'], ['--digest-info']):
        run([*era(), *extra], ok=False)
    run(['--make-digest', '-s', str(source), '-f', str(digest), '-E', '8'], ok=False)
    run([*era(), '-D', str(source)], ok=False)
    run([*era(), '-e', str(digest)], ok=False)
    for replacement in (b'', digest.read_bytes()[:-1]):
        old_digest = digest.read_bytes()
        digest.write_bytes(replacement)
        old = snapshot(digest)
        run([*era(), '--force'], ok=False)
        assert snapshot(digest) == old
        digest.write_bytes(old_digest)
    old = snapshot(digest)
    run([*era(), '-a', 'SHA256', '--force'], ok=False)
    assert snapshot(digest) == old
    digest.unlink()
    run(era(), ok=False)
    assert not digest.exists()
    print('PASS invalid XML / options / bounds / aliases / missing or incompatible baselines', flush=True)

    # An empty list reads no source bytes; selecting only the tail skips the large prefix.
    reset(8 * 1024 * 1024 + 7)
    read_log = root / 'reads'
    environment = dict(os.environ, LD_PRELOAD=probe, BSF_SOURCE=str(source), BSF_READ_LOG=str(read_log))
    xml.write_text('<blocks/>')
    run(era(), env=environment)
    assert not read_log.exists()
    xml.write_text('<blocks><block block="2048"/></blocks>')
    run(era(), env=environment)
    assert read_log.read_text().splitlines() == [f'{8 * 1024 * 1024} 7']
    print('PASS source read avoidance', flush=True)

    # Binary stdout delta headers and records survive short writes and EINTR.
    reset(8193)
    with source.open('r+b') as f:
        f.seek(4096)
        f.write(b'changed')
    xml.write_text('<blocks><range begin="0" end="3"/></blocks>')
    environment = dict(os.environ, LD_PRELOAD=probe, BSF_SHORT_STREAM='1')
    result = run([*era(), '--make-delta'], env=environment)
    delta.write_bytes(result.stdout)
    assert replay_independently() == [4096]
    assert digest.read_bytes()[512:] == full_body()
    print('PASS streamed delta with short writes and EINTR', flush=True)

    # Failure after header mutation must mark the baseline incomplete and reject a selective retry.
    for fault in ('BSF_FAIL_READ', 'BSF_FAIL_PATH'):
        reset(8192)
        xml.write_text('<blocks><block block="0"/></blocks>')
        environment = dict(os.environ, LD_PRELOAD=probe)
        environment[fault] = str(source if fault == 'BSF_FAIL_READ' else digest)
        run(era(), ok=False, env=environment)
        marker = root / 'digest.incomplete'
        assert marker.exists()
        run(era(), ok=False)
        digest.unlink()
        marker.unlink()
        run(['--make-digest', '-s', str(source), '-f', str(digest)])
        run(era())
        assert not marker.exists()
    print(f'PASS interrupted baseline refusal / full rebuild recovery; {calls} invocations', flush=True)

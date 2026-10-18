"""Verify actual Twister UART output, including rejection of real native loss."""
import argparse
from pathlib import Path
import shutil
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'target'))
from m3_image import check_record_loss


def verify(directory, announce=True):
    logs = list(directory.rglob('handler.log'))
    ordinary = [p for p in logs if p.parent.name == 'crux.native.deferred_output']
    overflow = [p for p in logs if p.parent.name == 'crux.native.deferred_overflow']
    if len(ordinary) != 1 or len(overflow) != 1:
        raise ValueError('Missing or ambiguous actual native handler logs')
    for log in (ordinary[0], overflow[0]):
        config = (log.parent / 'zephyr/.config').read_text().splitlines()
        if not {'CONFIG_BOARD_NATIVE_SIM=y', 'CONFIG_LOG_PRINTK=y', 'CONFIG_LOG_MODE_DEFERRED=y',
                'CONFIG_LOG_PROCESS_THREAD=y', 'CONFIG_LOG_BACKEND_UART=y'}.issubset(config):
            raise ValueError('Fixture did not use the native deferred UART path')
        backends = {line for line in config if line.startswith('CONFIG_LOG_BACKEND_') and line.endswith('=y')}
        allowed = {'CONFIG_LOG_BACKEND_UART=y', 'CONFIG_LOG_BACKEND_UART_AUTOSTART=y',
                   'CONFIG_LOG_BACKEND_UART_OUTPUT_TEXT=y', 'CONFIG_LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP=y'}
        if not backends.issubset(allowed):
            raise ValueError('Fixture has an extra logging backend')
    def rows(path):
        lines = path.read_bytes().decode('ascii').split('\n')
        return [(i, line[:-1] if line.endswith('\r') else line)
                for i, line in enumerate(lines)]
    normal = rows(ordinary[0])
    check_record_loss(normal)
    lines = [line for _, line in normal]
    expected = 'DEFERRED_RECORD signed=-9223372036854775807 unsigned=18446744073709551615 payload=' + 'abcdefghijklmnopqrstuvwxyz0123456789' * 6
    if not (lines.count('DEFERRED_BEGIN') == lines.count(expected) == lines.count('DEFERRED_COMPLETE') == 1 and
            lines.index('DEFERRED_BEGIN') < lines.index(expected) < lines.index('DEFERRED_COMPLETE')):
        raise ValueError('Truncated, duplicated, prefixed or reordered actual raw output')
    loss = rows(overflow[0])
    try:
        check_record_loss(loss)
    except ValueError:
        if sum(line == 'OVERFLOW_COMPLETE' for _, line in loss) != 1:
            raise ValueError('Overflow fixture did not complete')
    else:
        raise ValueError('Actual forced overflow was not rejected')
    if announce:
        print('ACTUAL_NATIVE_DEFERRED_RAW_AND_LOSS_CONTROLS_PASS; not target timing or qualification')


def replay_negative_controls(directory):
    originals = list(directory.rglob('handler.log'))
    normal = next(p for p in originals if p.parent.name == 'crux.native.deferred_output')
    loss = next(p for p in originals if p.parent.name == 'crux.native.deferred_overflow')
    original = normal.read_bytes()
    record = next(line for line in original.split(b'\n') if line.startswith(b'DEFERRED_RECORD '))
    changes = (b' ' + record, b'\x1b[31m' + record + b'\x1b[0m',
               record + b'\n' + record, record[:len(record)//2], record + b' ')
    for changed in changes:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for source in (normal, loss):
                destination = root / source.parent.name / 'handler.log'
                destination.parent.mkdir()
                shutil.copy2(source, destination)
                (destination.parent / 'zephyr').mkdir()
                shutil.copy2(source.parent / 'zephyr/.config', destination.parent / 'zephyr/.config')
            verify(root, announce=False)
            target = root / normal.parent.name / 'handler.log'
            target.write_bytes(original.replace(record, changed, 1))
            try:
                verify(root, announce=False)
            except ValueError:
                pass
            else:
                raise ValueError('Malformed actual-handler replay was accepted')
    print('ACTUAL_HANDLER_REPLAY_NEGATIVES_PASS; leading space, ANSI, duplicate, partial, trailing space')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    directory = parser.parse_args().directory
    verify(directory)
    replay_negative_controls(directory)

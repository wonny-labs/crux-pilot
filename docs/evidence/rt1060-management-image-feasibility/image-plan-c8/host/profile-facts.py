"""Read-only probe for the image profile input of row C8. Touches no hardware and writes nothing.

usage: python3 -I profile-facts.py REPO KEEP
  REPO  a checkout of the source that was built (for tests/target, read only)
  KEEP  a folder with the build's zephyr.bin, zephyr.elf, .config, bench-manifest.json and stack-symbols.txt

It asks the existing M5 and S2 image checks what they say about the bench image, compares the resolved configuration
with the M5 constants, compares the manifest's settings with the resolved configuration, and builds a candidate
ImageProfile (never registered) to see that the proposed pins and paths are consistent.
"""
import json
import re
import sys
from pathlib import Path

repo, keep = Path(sys.argv[1]), Path(sys.argv[2])
sys.path.insert(0, str(repo / 'tests/target'))
import m3_image          # noqa: E402
import m5_image          # noqa: E402
import s2_image          # noqa: E402
from image_profile import ImageProfile   # noqa: E402

binary, elf, config_path = keep / 'zephyr.bin', keep / 'zephyr.elf', keep / '.config'
config = config_path.read_text().splitlines()
settings = {}
for line in config:
    found = re.match(r'(CONFIG_[A-Za-z0-9_]+)=(.*)$', line)
    if found:
        settings[found.group(1)] = found.group(2)

print('== 1. geometry, the shared M3 check (boot header, ELF sections against the BIN, gap fill)')
geometry = m3_image.check_image_bytes(binary, elf)
print('pass: bin_bytes=%d, symbols read=%d' % (geometry['binary_bytes'], len(geometry['symbols'])))
for name in ('z_main_stack',):
    print('symbol %s: %s' % (name, geometry['symbols'].get(name)))

print('\n== 2. what the existing profiles say about this image')
for label, check in (('m5_image.check_image', m5_image.check_image), ('s2_image.check_image', s2_image.check_image)):
    try:
        check(binary, elf, config_path)
        print('%s: PASS' % label)
    except Exception as error:     # the checks raise ValueError or HardwareStop
        print('%s: refuses: %s' % (label, error))

print('\n== 3. the M5 constants against the resolved bench configuration')
source = (repo / 'tests/target/m5_image.py').read_text()
required = re.findall(r"'(CONFIG_[A-Z0-9_]+(?:=[^']*)?)'", source[source.index('required={'):source.index('require(required')])
forbidden = re.findall(r"'([A-Z0-9_]+)'", source[source.index('for feature in ('):source.index('):', source.index('for feature in ('))])
missing, different = [], []
for item in required:
    key, _, value = item.partition('=')
    if item in config:
        continue
    (different if key in settings else missing).append('%s (bench: %s)' % (item, settings.get(key, 'not set')))
print('M5 required lines: %d, met by the bench: %d' % (len(required), len(required) - len(missing) - len(different)))
print('  different value:')
for item in different:
    print('   ', item)
print('  not set at all:')
for item in missing:
    print('   ', item)
print('M5 forbidden features set to y in the bench configuration:',
      ', '.join(f for f in forbidden if 'CONFIG_%s=y' % f in config) or 'none')
print('M5 forbidden features not set:', ', '.join(f for f in forbidden if 'CONFIG_%s=y' % f not in config) or 'none')

print('\n== 4. the manifest the build wrote, against the resolved configuration')
manifest = json.loads((keep / 'bench-manifest.json').read_text())
bad = []
for key, value in sorted(manifest['settings'].items()):
    want = value if value in ('y', 'n') or re.fullmatch(r'-?\d+|0x[0-9a-fA-F]+', value) else '"%s"' % value
    have = settings.get(key)
    if have != want and not (value == 'n' and have is None):
        bad.append('%s manifest=%s config=%s' % (key, value, have))
print('manifest settings: %d, equal to .config (an n may be absent from it): %s' % (
    len(manifest['settings']), 'all' if not bad else 'NOT: ' + '; '.join(bad)))

print('\n== 5. stack objects of the linked image')
for line in (keep / 'stack-symbols.txt').read_text().splitlines():
    parts = line.split()
    if len(parts) == 4 and int(parts[1], 16) >= 1024:
        print('%s %6d B %s' % (parts[0], int(parts[1], 16), parts[3]))

print('\n== 6. a candidate profile, never registered (only its own consistency rule is run)')
pins = {
    'zephyr': ('dccb09599635bdff17633fa7e9dab014b91dce90', 'zephyr'),
    'cmsis': ('512cc7e895e8491696b61f7ba8066b4a182569b8', 'modules/hal/cmsis'),
    'cmsis_6': ('30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74', 'modules/hal/cmsis_6'),
    'hal_nxp': ('7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0', 'modules/hal/nxp'),
    'fatfs': ('f4ead3bf4a6dab3a07d7b5f5315795c073db568d', 'modules/fs/fatfs'),
    'wolfssl': ('3c5eead44904df64e6a5a1f4ebdce377d35a849a', 'modules/crypto/wolfssl'),
    'wolfssh': ('8221e4ee48c93da6cea3325a91de2ecc00ae5f56', 'modules/crypto/wolfssh'),
    'wolfpsa': ('55ae0ec58cd2ef4c528f44e8466095550c16ca8a', 'modules/crypto/wolfpsa'),
    'mbedtls': ('a3e190fe44c78d1ba67f55979e1257328cc7d0d8', 'modules/crypto/mbedtls'),
    'tf-psa-crypto': ('dc575a2ddcc8cb16275d24c42a52eaf79ebe2231', 'modules/crypto/tf-psa-crypto'),
}
candidate = ImageProfile(name=manifest['image_id'], manifest_path='crux-pilot',
                         pinned={k: v[0] for k, v in pins.items()}, pinned_paths={k: v[1] for k, v in pins.items()},
                         check_image=lambda *a: {}, commit_tag=False, errno_suffix=None,
                         tool_scripts=('record-build-provenance.py',), capture='raw', capture_seconds=300)
print('constructed: name=%s pinned=%d capture=%s/%d s' % (candidate.name, len(candidate.pinned), candidate.capture,
                                                           candidate.capture_seconds))
try:
    ImageProfile(**{**candidate.__dict__, 'capture_seconds': 600})
    print('capture_seconds=600: accepted')
except ValueError as error:
    print('capture_seconds=600: refused: %s' % error)

"""Every place that states the firmware version must agree with mttl-source.json."""
from pathlib import Path
import json
import re
import unittest

SOURCE = Path(__file__).resolve().parents[2]
VERSION = json.loads((SOURCE / 'mttl-source.json').read_text(encoding='utf-8'))['version']


def text(relative):
    return (SOURCE / relative).read_text(encoding='utf-8')


class VersionConsistency(unittest.TestCase):
    def test_format(self):
        self.assertRegex(VERSION, r'^(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})$')

    def test_firmware_header(self):
        self.assertIn('#define MTTL_FIRMWARE_VERSION "%s"' % VERSION, text('standalone/firmware/version.h'))

    def test_c_sources_use_header(self):
        # Only the pull-update unit test may carry literal versions (signed test vectors).
        for path in sorted((SOURCE / 'standalone/firmware').glob('*.[ch]*')):
            if path.name in ('pull_mock.c', 'version.h'):
                continue
            self.assertNotRegex(path.read_text(encoding='utf-8', errors='replace'), r'\\"(?:version|sw)\\":\\"\d', path.name)
        for name in ('sdk_web.c', 'ha_discovery.c', 'sdk_baseline.cpp'):
            self.assertIn('MTTL_FIRMWARE_VERSION', text('standalone/firmware/' + name), name)

    def test_ui(self):
        self.assertIn('<b id="current-firmware-version">%s</b>' % VERSION, text('standalone/ui/index.html'))
        self.assertIn('<h1>MTTL-W01 %s · OTA Test</h1>' % VERSION, text('standalone/ui/ota-test.html'))

    def test_source_readmes(self):
        for name in ('README.md', 'README_EN.md'):
            first = text(name).splitlines()[0]
            self.assertEqual(first, '# MTTL-W01 %s — Firmware source' % VERSION, name)

    def test_top_level_readmes(self):
        for name in ('README.md', 'README_EN.md'):
            body = (SOURCE.parent / name).read_text(encoding='utf-8')
            self.assertIn('### %s' % VERSION, body, name + ' changelog')


if __name__ == '__main__':
    unittest.main()

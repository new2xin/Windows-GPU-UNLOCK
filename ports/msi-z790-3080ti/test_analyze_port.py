"""Synthetic observer output; no firmware or hardware access."""
import hashlib
import struct
import tempfile
from pathlib import Path
import unittest
from analyze_port import analyze, image_sections


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        image = bytearray(4096)
        image[:2] = b'MZ'
        struct.pack_into('<I', image, 0x3c, 0x80)
        image[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', image, 0x84, 0x8664, 1)
        struct.pack_into('<H', image, 0x94, 0xf0)
        at = 0x80+24+0xf0
        image[at:at+8] = b'.text\0\0\0'
        struct.pack_into('<II', image, at+8, 512, 1024)
        self.image = bytes(image)
        self.prom = bytes(range(256))*4096
        (self.root/'PROM.BIN').write_bytes(self.prom)
        (self.root/'IMG000.BIN').write_bytes(self.image)
        cfg = [0]*64
        cfg[0], cfg[11] = 0x220810de, 0x22081569
        registers = [0, 0x1183a4, 0x1fa7c8, 0x1fa828, 0x1fa82c, 0x1fa830, 0x625f04]
        lines = ['port-target-count=0x0000000000000001',
                 'port-location seg=0x0000 bus=0x0001 dev=0x0000 fun=0x0000',
                 'port-config-dwords='+' '.join(f'{v:08x}' for v in cfg),
                 'port-bar0-base=0x00000000f5000000',
                 'port-bar0-length=0x0000000001000000']
        lines += [f'port-mmio off=0x{off:08x} status=0x0000000000000000 value=0x{0xb72000a1 if off == 0 else 0:08x}' for off in registers]
        lines += ['port-prom-sha256='+hashlib.sha256(self.prom).hexdigest(),
                  'port-binding handle=0x0000000000000001 image=0x0000000000000002 version=0x00060010 stop=0x0000000000100450',
                  'port-image index=0x0000 handle=0x0000000000000002 base=0x0000000000100000 bytes=0x0000000000001000',
                  'port-image sha256='+hashlib.sha256(self.image).hexdigest(),
                  'port-image-count=0x0000000000000001',
                  'port-image-total-bytes=0x0000000000001000',
                  'port-observation-only=1 can-unlock=0', 'capture-complete=1', '']
        self.text = '\n'.join(lines)
        self.save()

    def save(self):
        (self.root/'CAPTURE.TXT').write_text(self.text, encoding='utf-8')

    def test_candidate_never_authorizes_unlock(self):
        result = analyze(self.root)
        self.assertFalse(result['can_unlock'])
        self.assertEqual(result['status'], 'requires-review')
        self.assertEqual(result['images'][0]['bindings'][0]['stop_rva'], 0x450)
        self.assertEqual(result['images'][0]['sections'][0]['rva'], 1024)

    def test_partial_capture_refused(self):
        self.text = self.text.replace('capture-complete=1', 'capture-complete=0')
        self.save()
        with self.assertRaises(ValueError):
            analyze(self.root)

    def test_modified_prom_refused(self):
        (self.root/'PROM.BIN').write_bytes(self.prom[:-1]+b'\x00')
        with self.assertRaises(ValueError):
            analyze(self.root)

    def test_modified_image_refused(self):
        (self.root/'IMG000.BIN').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            analyze(self.root)

    def test_ambiguous_target_refused(self):
        self.text = self.text.replace('port-target-count=0x0000000000000001', 'port-target-count=0x0000000000000002')
        self.save()
        with self.assertRaises(ValueError):
            analyze(self.root)

    def test_duplicate_register_refused(self):
        self.text += 'port-mmio off=0x00000000 status=0x0000000000000000 value=0xb72000a1\n'
        self.save()
        with self.assertRaises(ValueError):
            analyze(self.root)

    def test_malformed_mapped_section_refused(self):
        image = bytearray(self.image)
        struct.pack_into('<II', image, 0x80+24+0xf0+8, 0x1000, 0x1000)
        with self.assertRaises(ValueError):
            image_sections(image)

    def test_duplicate_capture_field_refused(self):
        self.text += 'port-bar0-base=0x00000000f5000000\n'
        self.save()
        with self.assertRaises(ValueError):
            analyze(self.root)


if __name__ == '__main__':
    unittest.main()

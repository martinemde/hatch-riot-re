import hashlib
import struct
import unittest
from server import validate_application

def app():
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    header[23] = 1
    data = struct.pack("<I", 0xABCD5432) + bytes(28)
    image = header + struct.pack("<II", 0x3F400020, len(data)) + data
    checksum = 0xEF
    for byte in data:
        checksum ^= byte
    image += bytes((len(image) | 15) - len(image)) + bytes([checksum])
    return image + hashlib.sha256(image).digest()

class ApplicationValidation(unittest.TestCase):
    def test_application(self):
        validate_application(app())
    def test_rejected_inputs(self):
        valid = app()
        corrupt = bytearray(valid)
        corrupt[40] ^= 1
        for image in (bytes(4096)+valid, valid+bytes(4096), valid[:-1], corrupt, b"not firmware"):
            with self.subTest(length=len(image)), self.assertRaises(ValueError):
                validate_application(image)

if __name__ == "__main__":
    unittest.main()

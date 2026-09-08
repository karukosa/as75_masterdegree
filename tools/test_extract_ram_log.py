import csv
import struct
import tempfile
import unittest
from pathlib import Path

import extract_ram_log


class ExtractRamLogTest(unittest.TestCase):
    def test_decode_and_csv_values(self):
        header = extract_ram_log.HEADER.pack(
            extract_ram_log.MAGIC, 1, extract_ram_log.SAMPLE.size, 151, 2, 1
        )
        samples = b"".join(
            [
                extract_ram_log.SAMPLE.pack(0, 253, 70, 1, 0),
                extract_ram_log.SAMPLE.pack(1000, -15, 70, 0, 1),
            ]
        )
        rows, complete, offset, capacity = extract_ram_log.decode(b"prefix" + header + samples)
        self.assertEqual(offset, 6)
        self.assertTrue(complete)
        self.assertEqual(capacity, 151)
        self.assertEqual(rows[0], (0, 70, 1, 25.3, "RUN"))
        self.assertEqual(rows[1], (1000, 70, 0, -1.5, "DONE"))

    def test_rejects_missing_magic(self):
        with self.assertRaisesRegex(ValueError, "HTLOG001"):
            extract_ram_log.decode(bytes(64))


if __name__ == "__main__":
    unittest.main()

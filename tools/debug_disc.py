#!/usr/bin/env python3
"""Debug disc image parsing - examine raw directory data."""

import struct
import sys

SECTOR_SIZE_RAW = 2352
SECTOR_DATA_OFFSET = 16
SECTOR_DATA_SIZE = 2048

def read_sector(f, sector_num):
    f.seek(sector_num * SECTOR_SIZE_RAW + SECTOR_DATA_OFFSET)
    return f.read(SECTOR_DATA_SIZE)

def main():
    track3_path = "Crazy Taxi (USA) (Track 3).bin"

    with open(track3_path, 'rb') as f:
        # Read PVD
        pvd = read_sector(f, 16)
        print(f"PVD signature: {pvd[1:6]}")

        root_record = pvd[156:156+34]
        print(f"Root dir record (hex): {root_record.hex()}")

        root_lba = struct.unpack_from('<I', root_record, 2)[0]
        root_size = struct.unpack_from('<I', root_record, 10)[0]
        print(f"Root LBA: {root_lba}, Size: {root_size}")

        # Read root directory sectors
        sectors_needed = (root_size + SECTOR_DATA_SIZE - 1) // SECTOR_DATA_SIZE
        print(f"Reading {sectors_needed} sectors for root directory")

        dir_data = b''
        for i in range(sectors_needed):
            sector_data = read_sector(f, root_lba + i)
            dir_data += sector_data

        # Dump first 512 bytes of directory data
        print(f"\nRaw directory data (first 512 bytes):")
        for row in range(0, min(512, len(dir_data)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in dir_data[row:row+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in dir_data[row:row+16])
            print(f"  {row:04x}: {hex_str}  {ascii_str}")

        # Parse directory records manually
        print(f"\n=== Parsing directory records ===")
        offset = 0
        record_num = 0
        while offset < root_size:
            record_len = dir_data[offset]
            if record_len == 0:
                # Try next sector boundary
                next_sector = ((offset // SECTOR_DATA_SIZE) + 1) * SECTOR_DATA_SIZE
                if next_sector >= root_size:
                    break
                offset = next_sector
                continue

            record = dir_data[offset:offset + record_len]
            extent_lba = struct.unpack_from('<I', record, 2)[0]
            data_length = struct.unpack_from('<I', record, 10)[0]
            flags = record[25]
            name_len = record[32]

            raw_name = record[33:33 + name_len] if name_len > 0 else b''

            if name_len == 1 and raw_name == b'\x00':
                name = '.'
            elif name_len == 1 and raw_name == b'\x01':
                name = '..'
            else:
                name = raw_name.decode('ascii', errors='replace').split(';')[0]

            is_dir = bool(flags & 0x02)
            print(f"  Record {record_num}: len={record_len} name='{name}' "
                  f"name_len={name_len} lba={extent_lba} size={data_length:,} "
                  f"flags=0x{flags:02x} {'DIR' if is_dir else 'FILE'}")

            record_num += 1
            offset += record_len

        # Also check sector 0 for bootstrap info
        print("\n=== Sector 0 (IP.BIN) - first 384 bytes ===")
        ip_data = read_sector(f, 0)
        for row in range(0, 384, 16):
            hex_str = ' '.join(f'{b:02x}' for b in ip_data[row:row+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ip_data[row:row+16])
            print(f"  {row:04x}: {hex_str}  {ascii_str}")

if __name__ == '__main__':
    main()

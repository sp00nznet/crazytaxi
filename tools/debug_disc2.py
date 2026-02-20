#!/usr/bin/env python3
"""Debug disc with LBA offset correction for GD-ROM high-density area."""

import struct

SECTOR_SIZE_RAW = 2352
SECTOR_DATA_OFFSET = 16
SECTOR_DATA_SIZE = 2048
GD_ROM_HD_START = 45000  # Standard GD-ROM high-density area start LBA

def read_sector(f, sector_num):
    f.seek(sector_num * SECTOR_SIZE_RAW + SECTOR_DATA_OFFSET)
    return f.read(SECTOR_DATA_SIZE)

def main():
    track3_path = "Crazy Taxi (USA) (Track 3).bin"

    with open(track3_path, 'rb') as f:
        # Read PVD at sector 16 of the track
        pvd = read_sector(f, 16)
        print(f"PVD signature: {pvd[1:6]}")

        root_record = pvd[156:156+34]
        root_lba_abs = struct.unpack_from('<I', root_record, 2)[0]
        root_size = struct.unpack_from('<I', root_record, 10)[0]
        print(f"Root LBA (absolute): {root_lba_abs}")
        print(f"Root LBA (track-relative): {root_lba_abs - GD_ROM_HD_START}")
        print(f"Root size: {root_size}")

        # Read root directory with offset correction
        root_lba_rel = root_lba_abs - GD_ROM_HD_START
        dir_data = b''
        sectors_needed = (root_size + SECTOR_DATA_SIZE - 1) // SECTOR_DATA_SIZE
        for i in range(sectors_needed):
            dir_data += read_sector(f, root_lba_rel + i)

        # Dump first 512 bytes
        print(f"\nRoot directory data (first 512 bytes) at track sector {root_lba_rel}:")
        for row in range(0, min(512, len(dir_data)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in dir_data[row:row+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in dir_data[row:row+16])
            print(f"  {row:04x}: {hex_str}  {ascii_str}")

        # Parse directory records
        print(f"\n=== Directory records ===")
        offset = 0
        while offset < root_size:
            record_len = dir_data[offset]
            if record_len == 0:
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
            rel_lba = extent_lba - GD_ROM_HD_START
            print(f"  [{('DIR' if is_dir else 'FILE'):4s}] {name:30s} "
                  f"LBA_abs={extent_lba:8d} LBA_rel={rel_lba:8d} "
                  f"Size={data_length:12,}")

            offset += record_len

if __name__ == '__main__':
    main()

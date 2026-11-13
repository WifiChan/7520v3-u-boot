#!/usr/bin/env python3
"""
package.py - Package Sanechip ZX297520V3 Web Failsafe U-Boot into a
flash-ready self-extracting image.

Copyright (C) 2026 Wifi Chan Lab
"""
import os
import sys
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="Package ZX297520V3 Self-Extracting U-Boot")
    parser.add_argument("--uboot", default="../u-boot.bin", help="Path to uncompressed u-boot.bin")
    parser.add_argument("--compress-dir", default=".", help="Path to compress source directory")
    parser.add_argument("--header-from", default=None, help="Extract 384-byte signature header from an existing 2.uboot")
    parser.add_argument("--outdir", default=".", help="Output directory")
    parser.add_argument("--image-start", default="0x21DF0000", help="Decompress target memory address")
    args = parser.parse_args()

    compress_dir = os.path.abspath(args.compress_dir)
    uboot_bin = os.path.abspath(args.uboot)
    outdir = os.path.abspath(args.outdir)

    if not os.path.isfile(uboot_bin):
        print(f"Error: {uboot_bin} not found!")
        sys.exit(1)

    raw_size = os.path.getsize(uboot_bin)
    print(f"[+] Input uncompressed U-Boot: {uboot_bin} ({raw_size:,} bytes / {raw_size/1024:.1f} KB)")

    # 1. Build self-extracting compressed.bin
    print("[+] Building self-extracting compressed.bin ...")
    subprocess.run(["make", "-C", compress_dir, "clean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    cmd = [
        "make", "-C", compress_dir,
        f"INPUT_BIN={uboot_bin}",
        f"IMAGE_START={args.image_start}",
        "COMPRESS_TYPE=lzma",
        "CROSS_COMPILE=arm-linux-gnueabi-"
    ]
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        print("Error: make failed in compress/")
        sys.exit(1)

    compressed_bin = os.path.join(compress_dir, "compressed.bin")
    if not os.path.isfile(compressed_bin):
        print(f"Error: {compressed_bin} not found!")
        sys.exit(1)

    comp_size = os.path.getsize(compressed_bin)
    print(f"[+] Generated compressed.bin: {comp_size:,} bytes ({comp_size/1024:.1f} KB)")

    # 2. Generate uImage
    uimg_out = os.path.join(outdir, "uboot.uimg")
    print(f"[+] Generating uImage {uimg_out} ...")
    mkimg_cmd = [
        "mkimage", "-A", "arm", "-O", "u-boot", "-T", "kernel", "-C", "none",
        "-a", "0x21ef0000", "-e", "0x21ef0000", "-n", "ZX297520",
        "-d", compressed_bin, uimg_out
    ]
    subprocess.check_call(mkimg_cmd)
    uimg_data = open(uimg_out, "rb").read()
    uimg_size = len(uimg_data)
    print(f"[+] Generated uboot.uimg: {uimg_size:,} bytes ({uimg_size/1024:.1f} KB)")

    # 3. Create 384-byte ZTE signature header
    hdr384 = bytearray(384)
    if args.header_from and os.path.isfile(args.header_from):
        print(f"[+] Reusing 384-byte signature header from {args.header_from}")
        orig_hdr = open(args.header_from, "rb").read(384)
        hdr384[:len(orig_hdr)] = orig_hdr
    else:
        # Standard ZTE unsigned header: 124 zero bytes + 01 00 01 00 + 256 zero bytes
        hdr384[124:128] = b"\x01\x00\x01\x00"

    # 4. Generate flash-ready 2.uboot (padded with 0xFF to 160KB)
    flash_out = os.path.join(outdir, "2.uboot")
    full_flash_payload = bytes(hdr384) + uimg_data
    flash_size_raw = len(full_flash_payload)
    
    mf761w_budget = 160 * 1024
    am206_budget  = 192 * 1024
    
    # Pad to 160KB with 0xFF (standard flash erase state)
    pad_160k = max(0, mf761w_budget - flash_size_raw)
    payload_160k = full_flash_payload + (b"\xff" * pad_160k)
    with open(flash_out, "wb") as f:
        f.write(payload_160k)
    print(f"[+] Generated flash-ready 2.uboot (padded to 160KB with 0xFF): {len(payload_160k):,} bytes ({len(payload_160k)/1024:.1f} KB)")

    # Also generate 192KB version for AM206
    flash_out_192k = os.path.join(outdir, "2.uboot_192k.bin")
    pad_192k = max(0, am206_budget - flash_size_raw)
    payload_192k = full_flash_payload + (b"\xff" * pad_192k)
    with open(flash_out_192k, "wb") as f:
        f.write(payload_192k)
    print(f"[+] Generated flash-ready 2.uboot_192k.bin (padded to 192KB with 0xFF): {len(payload_192k):,} bytes ({len(payload_192k)/1024:.1f} KB)")

    # 5. Check partition limits
    print("\n" + "="*50)
    print("           PARTITION BUDGET CHECK")
    print("="*50)
    print(f"MF761W Budget : {mf761w_budget:,} bytes (160 KB)")
    print(f"AM206 Budget  : {am206_budget:,} bytes (192 KB)")
    print(f"Raw Code Size : {flash_size_raw:,} bytes ({flash_size_raw/1024:.1f} KB)")
    
    if flash_size_raw <= mf761w_budget:
        margin_mf = (mf761w_budget - flash_size_raw) / 1024
        print(f"-> MF761W (160KB): [PASS] Margin: {margin_mf:.1f} KB free (padded with {pad_160k:,} 0xFF bytes)")
    else:
        print(f"-> MF761W (160KB): [FAIL] Over by {(flash_size_raw - mf761w_budget)/1024:.1f} KB")

    if flash_size_raw <= am206_budget:
        margin_am = (am206_budget - flash_size_raw) / 1024
        print(f"-> AM206  (192KB): [PASS] Margin: {margin_am:.1f} KB free (padded with {pad_192k:,} 0xFF bytes)")
    else:
        print(f"-> AM206  (192KB): [FAIL] Over by {(flash_size_raw - am206_budget)/1024:.1f} KB")
    print("="*50 + "\n")


if __name__ == "__main__":
    main()

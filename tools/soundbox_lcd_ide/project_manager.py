"""
SoundBox LCD Project Manager & Exporter
Handles:
1. JSON project serialization (save / open).
2. Exporting screen previews as PNG, BMP, or PostScript.
3. Exporting Markdown stress-test reports.
4. Exporting C header struct stubs for firmware integration.
"""

import json
import os
import zlib
import struct
from typing import Dict, Any, Optional

class ProjectManager:
    """Manages project persistence and file exports."""

    @staticmethod
    def save_project(file_path: str, project_data: Dict[str, Any]) -> bool:
        """Saves preview configuration and screens to a JSON project file."""
        try:
            with open(file_path, 'w', encoding='utf-8') as f:
                json.dump(project_data, f, indent=2, ensure_ascii=False)
            return True
        except Exception as e:
            print(f"Error saving project: {e}")
            return False

    @staticmethod
    def load_project(file_path: str) -> Optional[Dict[str, Any]]:
        """Loads preview project from a JSON file."""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading project: {e}")
            return None

    @staticmethod
    def export_canvas_to_postscript(canvas, file_path: str) -> bool:
        """Exports the Tkinter canvas as an Encapsulated PostScript (.eps) file."""
        try:
            canvas.update()
            canvas.postscript(file=file_path, colormode='color')
            return True
        except Exception as e:
            print(f"Error exporting postscript: {e}")
            return False

    @staticmethod
    def export_raw_bmp(width: int, height: int, rgb_bytes: bytes, file_path: str) -> bool:
        """
        Exports RGB pixel buffer to a standard 24-bit uncompressed Windows BMP file.
        Pure Python implementation - zero external dependencies required!
        """
        try:
            row_size = (width * 3 + 3) & ~3  # 4-byte aligned rows
            image_size = row_size * height
            file_size = 54 + image_size

            # BMP File Header (14 bytes)
            header = struct.pack(
                '<2sIHHI',
                b'BM',
                file_size,
                0, 0,
                54  # Pixel data offset
            )

            # DIB Header (BITMAPINFOHEADER - 40 bytes)
            dib = struct.pack(
                '<IiiHHIIIIII',
                40,             # Header size
                width,
                -height,        # Top-down bitmap (signed int)
                1,              # Color planes
                24,             # Bits per pixel
                0,              # Compression (BI_RGB)
                image_size,
                2835, 2835,     # 72 DPI
                0, 0
            )

            # Pad rows to 4-byte boundary and convert RGB to BGR
            padded_rows = bytearray()
            stride = width * 3
            padding = b'\x00' * (row_size - stride)

            for y in range(height):
                row_start = y * stride
                row_pixels = rgb_bytes[row_start:row_start + stride]
                # Convert RGB to BGR
                bgr_row = bytearray(stride)
                for x in range(width):
                    r = row_pixels[x * 3]
                    g = row_pixels[x * 3 + 1]
                    b = row_pixels[x * 3 + 2]
                    bgr_row[x * 3] = b
                    bgr_row[x * 3 + 1] = g
                    bgr_row[x * 3 + 2] = r
                padded_rows.extend(bgr_row)
                padded_rows.extend(padding)

            with open(file_path, 'wb') as f:
                f.write(header)
                f.write(dib)
                f.write(padded_rows)
            return True
        except Exception as e:
            print(f"Error writing BMP: {e}")
            return False

    @staticmethod
    def export_raw_png(width: int, height: int, rgb_bytes: bytes, file_path: str) -> bool:
        """
        Pure Python PNG writer without requiring Pillow.
        Constructs standard PNG IHDR, IDAT (zlib deflated scanlines), and IEND chunks.
        """
        try:
            # PNG signature
            sig = b'\x89PNG\r\n\x1a\n'

            # IHDR Chunk
            ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
            ihdr_crc = struct.pack('>I', zlib.crc32(b'IHDR' + ihdr_data) & 0xFFFFFFFF)
            ihdr = struct.pack('>I', len(ihdr_data)) + b'IHDR' + ihdr_data + ihdr_crc

            # Scanlines: filter byte (0 = None) + RGB pixels per row
            scanlines = bytearray()
            stride = width * 3
            for y in range(height):
                scanlines.append(0)  # Filter byte 0
                scanlines.extend(rgb_bytes[y * stride:(y + 1) * stride])

            # Compress IDAT with zlib
            compressed = zlib.compress(scanlines, level=6)
            idat_crc = struct.pack('>I', zlib.crc32(b'IDAT' + compressed) & 0xFFFFFFFF)
            idat = struct.pack('>I', len(compressed)) + b'IDAT' + compressed + idat_crc

            # IEND Chunk
            iend_crc = struct.pack('>I', zlib.crc32(b'IEND') & 0xFFFFFFFF)
            iend = struct.pack('>I', 0) + b'IEND' + iend_crc

            with open(file_path, 'wb') as f:
                f.write(sig)
                f.write(ihdr)
                f.write(idat)
                f.write(iend)
            return True
        except Exception as e:
            print(f"Error writing PNG: {e}")
            return False

    @staticmethod
    def export_c_header_stub(screen_name: str, disp_buf: list) -> str:
        """
        Generates a C code snippet ready to paste into firmware app_lcd.c!
        """
        hex_vals = ", ".join(f"0x{b:02X}" for b in disp_buf)
        c_code = f"""/* Auto-generated by SoundBox LCD Preview IDE */
/* Screen: {screen_name} */
static const uint8_t s_custom_screen_{screen_name.lower()}[18] = {{
    {hex_vals}
}};

void appLcdShow_{screen_name.lower()}(void)
{{
    osiMutexLock(lcdFlagMux);
    memcpy(disp_buf, s_custom_screen_{screen_name.lower()}, 18);
    dispDma(0xC0, disp_buf, 15);
    osiMutexUnlock(lcdFlagMux);
}}
"""
        return c_code

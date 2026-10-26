"""
SoundBox LCD Font & Glyph Manager
Handles:
1. Exact 7-segment fonts from firmware (seg_map, hitachi_patterns, lit_patterns).
2. General ASCII to 7-segment character encoding and unsupported glyph detection.
3. Graphic LCD embedded bitmap fonts (5x7, 8x16, scalable) and text measurement.
"""

from typing import Tuple, Dict, List, Optional

# ============================================================================
# Firmware Source-Backed 7-Segment Definitions (app_lcd.c)
# ============================================================================

# Exact 7-segment mapping from app_lcd.c:88-99
# Bit 0: A (top)
# Bit 1: B (top-right)
# Bit 2: C (bottom-right)
# Bit 3: D (bottom)
# Bit 4: E (bottom-left)
# Bit 5: F (top-left)
# Bit 6: G (middle)
FIRMWARE_SEG_MAP_DIGITS = [
    0x3F,  # 0: A+B+C+D+E+F
    0x06,  # 1: B+C
    0x5B,  # 2: A+B+D+E+G
    0x4F,  # 3: A+B+C+D+G
    0x66,  # 4: B+C+F+G
    0x6D,  # 5: A+C+D+F+G
    0x7D,  # 6: A+C+D+E+F+G
    0x07,  # 7: A+B+C
    0x7F,  # 8: A+B+C+D+E+F+G
    0x6F   # 9: A+B+C+D+F+G
]

# Exact "HITACHI" patterns from app_lcd.c:1394-1403 (Original firmware with double-stem T)
FIRMWARE_HITACHI_PATTERNS = [
    0x76,  # 'H' (digit 0: B+C+E+F+G)
    0x06,  # 'I' (digit 1: B+C)
    0x07,  # 'T' left half  (digit 2: A+B+C ───┐)
    0x31,  # 'T' right half (digit 3: A+E+F ┌───)
    0x77,  # 'A' (digit 4: A+B+C+E+F+G)
    0x39,  # 'C' (digit 5: A+D+E+F)
    0x76,  # 'H' (digit 6: B+C+E+F+G)
    0x06   # 'I' (digit 7: B+C)
]

# Optimized Single-Stem "HITACHI" patterns: removes the extra 'I' (E+F) from Digit 3
HITACHI_PATTERNS_CLEAN_T = [
    0x76,  # 'H' (digit 0)
    0x06,  # 'I' (digit 1)
    0x07,  # 'T' left half  (digit 2: A+B+C ───┐: top bar + center stem)
    0x01,  # 'T' right half (digit 3: A only ─── : top bar only, extra 'I' removed!)
    0x77,  # 'A' (digit 4)
    0x39,  # 'C' (digit 5)
    0x76,  # 'H' (digit 6)
    0x06   # 'I' (digit 7)
]

# Exact "LIT" patterns from app_lcd.c:362
FIRMWARE_LIT_PATTERNS = [
    0x38,  # 'L' (D+E+F)
    0x06,  # 'I' (B+C)
    0x78   # 't' (D+E+F+G)
]

# Extended ASCII character map for 7-segment simulation
# None indicates the character cannot be natively represented on a 7-segment display.
ASCII_7SEG_MAP: Dict[str, Optional[int]] = {
    '0': 0x3F, '1': 0x06, '2': 0x5B, '3': 0x4F, '4': 0x66,
    '5': 0x6D, '6': 0x7D, '7': 0x07, '8': 0x7F, '9': 0x6F,
    'A': 0x77, 'a': 0x5F,
    'B': 0x7C, 'b': 0x7C,  # 'b'
    'C': 0x39, 'c': 0x58,
    'D': 0x5E, 'd': 0x5E,  # 'd'
    'E': 0x79, 'e': 0x7B,
    'F': 0x71, 'f': 0x71,
    'G': 0x3D, 'g': 0x6F,
    'H': 0x76, 'h': 0x74,
    'I': 0x06, 'i': 0x04,
    'J': 0x1E, 'j': 0x1E,
    'K': None, 'k': None,  # Unsupported on 7-seg
    'L': 0x38, 'l': 0x30,
    'M': None, 'm': None,  # Unsupported on 7-seg
    'N': 0x37, 'n': 0x54,
    'O': 0x3F, 'o': 0x5C,
    'P': 0x73, 'p': 0x73,
    'Q': 0x67, 'q': 0x67,
    'R': 0x77, 'r': 0x50,
    'S': 0x6D, 's': 0x6D,
    'T': None, 't': 0x78,  # Lowercase 't' supported; uppercase T requires 2 digits or 14-seg
    'U': 0x3E, 'u': 0x1C,
    'V': None, 'v': 0x1C,  # Approximate
    'W': None, 'w': None,  # Unsupported on 7-seg
    'X': None, 'x': None,  # Unsupported on 7-seg
    'Y': 0x6E, 'y': 0x6E,
    'Z': 0x5B, 'z': 0x5B,
    '-': 0x40, '_': 0x08, '=': 0x48, ' ': 0x00,
    '[': 0x39, ']': 0x0F, 'o': 0x5C, 'r': 0x50
}


def encode_7segment_char(char: str) -> Tuple[int, bool, bool]:
    """
    Encode a single character for a 7-segment digit.
    Returns: (segment_byte, is_supported, is_approximated)
    """
    if char in ASCII_7SEG_MAP:
        pattern = ASCII_7SEG_MAP[char]
        if pattern is not None:
            is_approx = char in ('V', 'v', 'R', 'N', 'Z', 'z')
            return pattern, True, is_approx
        return 0x00, False, False
    return 0x00, False, False


def encode_7segment_string(text: str, max_digits: int = 8, t_style: str = "original") -> List[Dict]:
    """
    Parse a string (including decimal points) into a list of 7-segment digit definitions.
    Handles '.' attached to preceding digit.
    t_style: 'original' (double-stem 0x07, 0x31) or 'clean' (single-stem 0x07, 0x01 with extra 'I' removed)
    Returns list of dicts: {'pattern': int, 'has_dot': bool, 'char': str, 'supported': bool}
    """
    digits = []
    i = 0
    text_len = len(text)
    
    # Check if exact string is "HITACHI" (special 8-digit firmware layout)
    if text.upper() == "HITACHI" and max_digits >= 8:
        patterns = HITACHI_PATTERNS_CLEAN_T if t_style == "clean" else FIRMWARE_HITACHI_PATTERNS
        hitachi_labels = ["H", "I", "T", "T" if t_style != "clean" else " ", "A", "C", "H", "I"]
        for idx, pat in enumerate(patterns):
            digits.append({
                'pattern': pat,
                'has_dot': False,
                'char': hitachi_labels[idx],
                'supported': True,
                'is_special_hitachi': True
            })
        return digits

    # Check if exact string is "LIT"
    if text.upper() == "LIT":
        for idx, pat in enumerate(FIRMWARE_LIT_PATTERNS):
            digits.append({
                'pattern': pat,
                'has_dot': False,
                'char': "LIT"[idx],
                'supported': True,
                'is_special': True
            })
        return digits

    while i < text_len and len(digits) < max_digits:
        c = text[i]
        if c == '.':
            # Standalone dot or dot after previous digit
            if digits:
                digits[-1]['has_dot'] = True
            else:
                digits.append({'pattern': 0, 'has_dot': True, 'char': '.', 'supported': True})
            i += 1
            continue

        pat, supp, approx = encode_7segment_char(c)
        has_dot = False
        if i + 1 < text_len and text[i + 1] == '.':
            has_dot = True
            i += 1

        digits.append({
            'pattern': pat,
            'has_dot': has_dot,
            'char': c,
            'supported': supp,
            'approximated': approx
        })
        i += 1

    return digits


# ============================================================================
# Graphic LCD Fonts & Metrics
# ============================================================================

class GraphicFontMetrics:
    """Provides exact text dimension calculations for Graphic LCDs."""

    # Built-in font profiles matching SoundBox firmware SDK
    FONTS = {
        'ASCII_5x7': {'char_w': 5, 'char_h': 7, 'line_spacing': 2, 'name': 'ASCII 5x7 (Small)'},
        'ASCII_8x16': {'char_w': 8, 'char_h': 16, 'line_spacing': 3, 'name': 'ASCII 8x16 (Medium)'},
        'ASCII_16x24': {'char_w': 16, 'char_h': 24, 'line_spacing': 4, 'name': 'ASCII 16x24 (Large)'},
        'ASCII_24x24': {'char_w': 24, 'char_h': 24, 'line_spacing': 4, 'name': 'ASCII 24x24 (Title)'},
        'Custom': {'char_w': 10, 'char_h': 18, 'line_spacing': 3, 'name': 'Proportional / TTF'}
    }

    @classmethod
    def measure_text(cls, text: str, font_name: str, font_size: int = 12) -> Tuple[int, int]:
        """
        Calculates pixel width and height for a string on a graphic LCD.
        """
        if font_name in cls.FONTS and font_name != 'Custom':
            info = cls.FONTS[font_name]
            lines = text.split('\n')
            max_len = max(len(line) for line in lines) if lines else 0
            w = max_len * info['char_w']
            h = len(lines) * info['char_h'] + max(0, len(lines) - 1) * info['line_spacing']
            return w, h
        else:
            # Proportional estimate based on average character width ~0.6 * size
            lines = text.split('\n')
            char_w = max(4, int(font_size * 0.6))
            char_h = max(8, int(font_size * 1.2))
            max_len = max(len(line) for line in lines) if lines else 0
            w = max_len * char_w
            h = len(lines) * char_h
            return w, h

    @classmethod
    def wrap_text(cls, text: str, max_width_px: int, font_name: str, font_size: int = 12) -> List[str]:
        """
        Wraps text to fit within max_width_px using firmware-like line breaking.
        """
        words = text.split(' ')
        lines = []
        current_line = ""

        for word in words:
            candidate = current_line + (" " if current_line else "") + word
            w, _ = cls.measure_text(candidate, font_name, font_size)
            if w <= max_width_px or not current_line:
                current_line = candidate
            else:
                lines.append(current_line)
                current_line = word

        if current_line:
            lines.append(current_line)

        return lines if lines else [""]

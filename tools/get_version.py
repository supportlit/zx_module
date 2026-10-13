#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 app_version.h 提取版本字符串
用于 download.bat 中获取 APP_VERSION_STRING
"""

import sys
import re
from pathlib import Path


def extract_version_string(header_file):
    """
    从 app_version.h 中提取 APP_VERSION_STRING
    
    Args:
        header_file: app_version.h 文件路径
        
    Returns:
        str: 版本字符串，如果失败返回 "Unknown"
    """
    try:
        if not Path(header_file).exists():
            print(f"Warning: {header_file} not found", file=sys.stderr)
            return "Unknown"
        
        with open(header_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 匹配 #define APP_VERSION_STRING "xxxxx"
        pattern = r'#define\s+APP_VERSION_STRING\s+"([^"]+)"'
        match = re.search(pattern, content)
        
        if match:
            version = match.group(1)
            return version
        else:
            print("Warning: APP_VERSION_STRING not found in header", file=sys.stderr)
            return "Unknown"
    
    except Exception as e:
        print(f"Error: Failed to extract version: {e}", file=sys.stderr)
        return "Unknown"


def main():
    if len(sys.argv) < 2:
        print("Usage: get_version.py <app_version.h>", file=sys.stderr)
        print("Unknown")
        sys.exit(1)
    
    header_file = sys.argv[1]
    version = extract_version_string(header_file)
    print(version)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Audio Resource Map Generator V2 - Dual Layer Architecture

Generates two files:
1. framework_audio_defs.h - Framework layer enum definitions (all resource IDs)
2. customer audio_resource_map.h - Customer layer file path mappings

Architecture:
- Framework defines ALL resource IDs (enums)
- Customer CSV only lists files they provide
- Resources without files get NULL path
- Playback checks for NULL and skips if empty

CSV Format: RESOURCE_ID,filename,description

Usage:
    python gen_audio_resources_v2.py <csv_file> <customer_header> <customer_name>
"""

import csv
import sys
import os

# Define ALL framework resource IDs (must match framework_audio.h enum)
FRAMEWORK_RESOURCES = [
    # System Prompts (0-9)
    ('AUDIO_RES_SYS_STARTUP', 'System startup sound'),
    ('AUDIO_RES_SYS_SHUTDOWN', 'System shutdown sound'),
    ('AUDIO_RES_SYS_READY', 'System ready prompt'),
    ('AUDIO_RES_SYS_ERROR', 'System error prompt'),
    ('AUDIO_RES_SYS_SUCCESS', 'Operation success'),
    ('AUDIO_RES_SYS_FAILURE', 'Operation failure'),
    ('AUDIO_RES_SYS_TIMEOUT', 'Operation timeout'),
    ('AUDIO_RES_SYS_CANCEL', 'Operation cancelled'),
    ('AUDIO_RES_SYS_CONFIRM', 'Confirmation prompt'),
    ('AUDIO_RES_SYS_DENY', 'Denial prompt'),
    
    # Volume Indicators (10-19)
    ('AUDIO_RES_VOL_MIN', 'Volume minimum'),
    ('AUDIO_RES_VOL_LOW', 'Volume low'),
    ('AUDIO_RES_VOL_MEDIUM', 'Volume medium'),
    ('AUDIO_RES_VOL_HIGH', 'Volume high'),
    ('AUDIO_RES_VOL_MAX', 'Volume maximum'),
    ('AUDIO_RES_VOL_MUTE', 'Volume muted'),
    ('AUDIO_RES_VOL_UNMUTE', 'Volume unmuted'),
    ('AUDIO_RES_VOL_UP', 'Volume up'),
    ('AUDIO_RES_VOL_DOWN', 'Volume down'),
    
    # Battery Status (20-29)
    ('AUDIO_RES_BAT_FULL', 'Battery full'),
    ('AUDIO_RES_BAT_HIGH', 'Battery high'),
    ('AUDIO_RES_BAT_MEDIUM', 'Battery medium'),
    ('AUDIO_RES_BAT_LOW', 'Battery low'),
    ('AUDIO_RES_BAT_CRITICAL', 'Battery critical'),
    ('AUDIO_RES_BAT_CHARGING', 'Battery charging'),
    ('AUDIO_RES_BAT_NOT_CHARGING', 'Battery not charging'),
    ('AUDIO_RES_BAT_CHARGE_COMPLETE', 'Charging complete'),
    
    # Charging Status (30-39)
    ('AUDIO_RES_CHG_CONNECTED', 'Charger connected'),
    ('AUDIO_RES_CHG_DISCONNECTED', 'Charger disconnected'),
    ('AUDIO_RES_CHG_FAST_CHARGING', 'Fast charging mode'),
    ('AUDIO_RES_CHG_SLOW_CHARGING', 'Slow charging mode'),
    ('AUDIO_RES_CHG_ERROR', 'Charging error'),
    ('AUDIO_RES_CHG_OVERHEATING', 'Overheating warning'),
    
    # Network Status (40-59)
    ('AUDIO_RES_NET_CONNECTED', 'Network connected'),
    ('AUDIO_RES_NET_DISCONNECTED', 'Network disconnected'),
    ('AUDIO_RES_NET_CONNECTING', 'Network connecting'),
    ('AUDIO_RES_NET_WIFI_ON', 'WiFi turned on'),
    ('AUDIO_RES_NET_WIFI_OFF', 'WiFi turned off'),
    ('AUDIO_RES_NET_WIFI_WEAK', 'WiFi signal weak'),
    ('AUDIO_RES_NET_WIFI_STRONG', 'WiFi signal strong'),
    ('AUDIO_RES_NET_4G_ON', '4G network on'),
    ('AUDIO_RES_NET_4G_OFF', '4G network off'),
    ('AUDIO_RES_NET_BT_CONNECTED', 'Bluetooth connected'),
    ('AUDIO_RES_NET_BT_DISCONNECTED', 'Bluetooth disconnected'),
    ('AUDIO_RES_NET_BT_PAIRING', 'Bluetooth pairing'),
    ('AUDIO_RES_NET_NO_SIGNAL', 'No network signal'),
    ('AUDIO_RES_NET_ROAMING', 'Network roaming'),
    ('AUDIO_RES_NET_SERVER_ERROR', 'Server connection error'),
    
    # Numbers 0-9 (60-69)
    ('AUDIO_RES_NUM_0', 'Number 0'),
    ('AUDIO_RES_NUM_1', 'Number 1'),
    ('AUDIO_RES_NUM_2', 'Number 2'),
    ('AUDIO_RES_NUM_3', 'Number 3'),
    ('AUDIO_RES_NUM_4', 'Number 4'),
    ('AUDIO_RES_NUM_5', 'Number 5'),
    ('AUDIO_RES_NUM_6', 'Number 6'),
    ('AUDIO_RES_NUM_7', 'Number 7'),
    ('AUDIO_RES_NUM_8', 'Number 8'),
    ('AUDIO_RES_NUM_9', 'Number 9'),
]

def load_customer_files(csv_file):
    """Load customer-provided audio files from CSV"""
    customer_files = {}
    
    if not os.path.exists(csv_file):
        print(f"Warning: CSV file '{csv_file}' not found, using empty map")
        return customer_files
    
    try:
        with open(csv_file, 'r', encoding='utf-8') as f:
            reader = csv.reader(f)
            line_num = 0
            for line in reader:
                line_num += 1
                
                if not line or len(line) == 0:
                    continue
                if line[0].strip().startswith('#'):
                    continue
                if line_num == 1 and line[0].strip().upper() == 'RESOURCE_ID':
                    continue
                
                if len(line) >= 2:
                    resource_id = line[0].strip()
                    filename = line[1].strip()
                    
                    if resource_id and filename:
                        customer_files[resource_id] = filename
    except Exception as e:
        print(f"Error reading CSV file: {e}")
        return {}
    
    return customer_files

def generate_customer_header(customer_files, output_file, customer_name):
    """Generate customer layer audio_resource_map.h"""
    
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            guard_name = f"CUSTOMER_{customer_name.upper()}_AUDIO_RESOURCE_MAP_H"
            
            # File header
            f.write(f"/* Auto-generated audio resource map for {customer_name} customer */\n")
            f.write(f"/* DO NOT EDIT THIS FILE MANUALLY */\n\n")
            
            f.write(f"#ifndef {guard_name}\n")
            f.write(f"#define {guard_name}\n\n")
            
            f.write("#include \"framework_audio.h\"\n\n")
            
            f.write("/**\n")
            f.write(" * Customer Audio Resource File Path Mapping\n")
            f.write(" * \n")
            f.write(" * Maps framework resource IDs to customer-provided audio files.\n")
            f.write(" * Resources without files are mapped to NULL.\n")
            f.write(" * Framework checks for NULL before playing.\n")
            f.write(" */\n\n")
            
            # Resource map array
            f.write("typedef struct {\n")
            f.write("    audio_resource_id_t id;\n")
            f.write("    const char *filepath;  /* NULL if not provided by customer */\n")
            f.write("} customer_audio_map_entry_t;\n\n")
            
            f.write("static const customer_audio_map_entry_t CUSTOMER_AUDIO_MAP[] = {\n")
            
            provided_count = 0
            missing_count = 0
            
            for res_id, description in FRAMEWORK_RESOURCES:
                filepath = customer_files.get(res_id, None)
                
                if filepath:
                    f.write(f"    {{{res_id:40s}, \"{filepath}\"}},  /* {description} */\n")
                    provided_count += 1
                else:
                    f.write(f"    {{{res_id:40s}, NULL}},  /* {description} - NOT PROVIDED */\n")
                    missing_count += 1
            
            f.write("};\n\n")
            
            f.write(f"#define CUSTOMER_AUDIO_MAP_COUNT {len(FRAMEWORK_RESOURCES)}\n")
            f.write(f"#define CUSTOMER_AUDIO_PROVIDED_COUNT {provided_count}\n")
            f.write(f"#define CUSTOMER_AUDIO_MISSING_COUNT {missing_count}\n\n")
            
            # Helper function declaration
            f.write("/**\n")
            f.write(" * Get customer audio file path by resource ID\n")
            f.write(" * @param res_id Resource ID\n")
            f.write(" * @return File path or NULL if not provided\n")
            f.write(" */\n")
            f.write("static inline const char* customer_audio_get_path(audio_resource_id_t res_id)\n")
            f.write("{\n")
            f.write("    if (res_id >= CUSTOMER_AUDIO_MAP_COUNT) {\n")
            f.write("        return NULL;\n")
            f.write("    }\n")
            f.write("    return CUSTOMER_AUDIO_MAP[res_id].filepath;\n")
            f.write("}\n\n")
            
            f.write(f"#endif /* {guard_name} */\n")
    
    except Exception as e:
        print(f"Error writing customer header: {e}")
        return False
    
    return True

def main():
    if len(sys.argv) != 4:
        print("Usage: python gen_audio_resources_v2.py <csv_file> <customer_header> <customer_name>")
        print("\nExample:")
        print("  python gen_audio_resources_v2.py customers/template/audio_resources.csv \\")
        print("         customers/template/audio_resource_map.h template")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    customer_header = sys.argv[2]
    customer_name = sys.argv[3]
    
    print(f"Generating audio resource map for customer '{customer_name}'")
    print(f"CSV input: {csv_file}")
    print(f"Customer header: {customer_header}")
    
    # Load customer files
    customer_files = load_customer_files(csv_file)
    print(f"Loaded {len(customer_files)} customer-provided audio files")
    
    # Generate customer header
    if generate_customer_header(customer_files, customer_header, customer_name):
        provided = len(customer_files)
        total = len(FRAMEWORK_RESOURCES)
        missing = total - provided
        
        print(f"\nGeneration successful!")
        print(f"  Total framework resources: {total}")
        print(f"  Customer provided: {provided}")
        print(f"  Missing (will be NULL): {missing}")
        
        if missing > 0:
            print(f"\n  Note: {missing} resources are not provided by customer.")
            print(f"  Framework will skip playback for these resources.")
        
        sys.exit(0)
    else:
        print("\nGeneration failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()

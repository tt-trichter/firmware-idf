#!/usr/bin/env python3
"""
Convert PNG image to LVGL C array format for monochrome displays
Requires: Pillow (pip install Pillow)
"""

import sys
import os
from PIL import Image
import argparse

def convert_to_lvgl_c_array(image_path, output_path, var_name):
    """Convert image to LVGL C array format for monochrome display"""
    
    # Open and process the image
    img = Image.open(image_path)
    
    # Convert to grayscale first
    img = img.convert('L')
    
    # Resize to 128x64 if needed
    img = img.resize((128, 64), Image.Resampling.LANCZOS)
    
    # Convert to 1-bit (monochrome)
    # Threshold at 128 - pixels darker than this become black (1), lighter become white (0)
    img = img.point(lambda p: 0 if p > 128 else 1, mode='1')
    
    width, height = img.size
    
    # Convert to byte array
    # LVGL expects data in rows, with each byte containing 8 horizontal pixels
    byte_array = []
    
    for y in range(height):
        for x in range(0, width, 8):
            byte_val = 0
            for bit in range(8):
                if x + bit < width:
                    pixel = img.getpixel((x + bit, y))
                    if pixel:  # If pixel is 1 (black)
                        byte_val |= (1 << (7 - bit))
            byte_array.append(byte_val)
    
    # Generate C header file
    header_content = f"""/*
 * Generated from {os.path.basename(image_path)}
 * Size: {width}x{height} pixels
 * Format: Monochrome (1 bit per pixel)
 */

#ifndef {var_name.upper()}_H
#define {var_name.upper()}_H

#include "lvgl.h"

#define {var_name.upper()}_WIDTH {width}
#define {var_name.upper()}_HEIGHT {height}

/* Image data in LVGL format */
static const uint8_t {var_name}_data[] = {{
"""
    
    # Add the byte data
    for i, byte in enumerate(byte_array):
        if i % 16 == 0:
            header_content += "\n    "
        header_content += f"0x{byte:02X}"
        if i < len(byte_array) - 1:
            header_content += ", "
    
    header_content += f"""
}};

/* LVGL image descriptor for LVGL v8 */
static const lv_img_dsc_t {var_name} = {{
    .header.cf = LV_IMG_CF_ALPHA_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {width},
    .header.h = {height},
    .data_size = {len(byte_array)},
    .data = {var_name}_data,
}};

/* Declare the image for external use */
LV_IMG_DECLARE({var_name});

#endif /* {var_name.upper()}_H */
"""
    
    # Write to file
    with open(output_path, 'w') as f:
        f.write(header_content)
    
    print(f"Converted {image_path} -> {output_path}")
    print(f"Image size: {width}x{height}, Data size: {len(byte_array)} bytes")

def main():
    parser = argparse.ArgumentParser(description='Convert image to LVGL C array format')
    parser.add_argument('input', help='Input image file (PNG, JPG, etc.)')
    parser.add_argument('-o', '--output', help='Output C header file')
    parser.add_argument('-n', '--name', default='img_icon', help='Variable name (default: img_icon)')
    
    args = parser.parse_args()
    
    if not args.output:
        # Generate output filename based on input
        base_name = os.path.splitext(os.path.basename(args.input))[0]
        args.output = f"main/include/{args.name}.h"
    
    # Ensure output directory exists
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    
    convert_to_lvgl_c_array(args.input, args.output, args.name)

if __name__ == "__main__":
    main()

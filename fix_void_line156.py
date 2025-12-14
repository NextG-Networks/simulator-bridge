#!/usr/bin/env python3
"""
Script to fix DecodeRicControlMessage return type at line 156
"""

# Read the file
with open('contrib/oran-interface/model/ric-control-message.cc', 'r') as f:
    lines = f.readlines()

# Fix line 156 (0-indexed as 155)
if 'RicControlMessage::DecodeRicControlMessage' in lines[155]:
    lines[155] = 'void\n' + lines[155]

# Write back
with open('contrib/oran-interface/model/ric-control-message.cc', 'w') as f:
    f.writelines(lines)

print("Successfully added void return type to DecodeRicControlMessage at line 156")

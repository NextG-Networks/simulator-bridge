#!/usr/bin/env python3
"""
Script to fix DecodeRicControlMessage return type
"""

# Read the file
with open('contrib/oran-interface/model/ric-control-message.cc', 'r') as f:
    lines = f.readlines()

# Find the line with DecodeRicControlMessage and add void before it
for i, line in enumerate(lines):
    if line.strip().startswith('RicControlMessage::DecodeRicControlMessage'):
        # Insert 'void\n' before this line
        lines[i] = 'void\n' + line
        break

# Write back
with open('contrib/oran-interface/model/ric-control-message.cc', 'w') as f:
    f.writelines(lines)

print("Successfully added void return type to DecodeRicControlMessage")

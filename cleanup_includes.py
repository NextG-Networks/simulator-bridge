#!/usr/bin/env python3
"""
Script to clean up includes and add mmwave headers to ric-control-message.cc
"""

# Read the file
with open('contrib/oran-interface/model/ric-control-message.cc', 'r') as f:
    lines = f.readlines()

# Find where includes end (before namespace ns3)
namespace_line = -1
for i, line in enumerate(lines):
    if 'namespace ns3' in line:
        namespace_line = i
        break

if namespace_line == -1:
    print("ERROR: Could not find 'namespace ns3'")
    exit(1)

# Keep the copyright header and first include
header_end = -1
for i, line in enumerate(lines):
    if '#include' in line:
        header_end = i
        break

# Extract copyright header
copyright_header = lines[:header_end]

# Define the clean includes we need
clean_includes = '''#include "ric-control-message.h"
#include <ns3/asn1c-types.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/mobility-model.h>
#include <ns3/mmwave-enb-net-device.h>
#include <ns3/mmwave-enb-mac.h>
#include <ns3/mmwave-enb-phy.h>
#include <bitset>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

'''

# Keep everything from namespace onwards
rest_of_file = lines[namespace_line:]

# Combine
new_content = ''.join(copyright_header) + clean_includes + ''.join(rest_of_file)

# Write back
with open('contrib/oran-interface/model/ric-control-message.cc', 'w') as f:
    f.write(new_content)

print("Successfully cleaned up includes and added mmwave headers")

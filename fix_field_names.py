#!/usr/bin/env python3
"""
Script to fix ParseSimpleCommand field names
"""

# Read the file
with open('contrib/oran-interface/model/ric-control-message.cc', 'r') as f:
    content = f.read()

# Replace the field names in ParseSimpleCommand
content = content.replace('json.find("\\"command\\"")', 'json.find("\\"cmd\\"")')
content = content.replace('json.find("\\"nodeId\\"")', 'json.find("\\"node\\"")')
content = content.replace('json.find("\\"txPower\\"")', 'json.find("\\"txPowerDbm\\"")')

# Write back
with open('contrib/oran-interface/model/ric-control-message.cc', 'w') as f:
    f.write(content)

print("Successfully updated field names in ParseSimpleCommand")

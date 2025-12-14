#!/usr/bin/env python3
"""
Script to update ric-control-message.h
"""

# Read the file
with open('contrib/oran-interface/model/ric-control-message.h', 'r') as f:
    content = f.read()

# Remove the ControlCommand struct (lines 52-57)
# Find and remove the struct definition
import re
content = re.sub(
    r'struct ControlCommand \{[^}]+\};\s*',
    '',
    content,
    flags=re.DOTALL
)

# Replace ParseSimpleCommand with ApplySimpleCommand
content = content.replace(
    '// static void ApplySimpleCommand(const std::string& json); // Removed to break dependency\n    void ParseSimpleCommand(const std::string& json);',
    'static void ApplySimpleCommand(const std::string& json);'
)

# Also try alternative format
content = content.replace(
    'void ParseSimpleCommand(const std::string& json);',
    'static void ApplySimpleCommand(const std::string& json);'
)

# Remove m_commands member variable
content = re.sub(
    r'std::vector<ControlCommand>\s+m_commands;\s*\n',
    '',
    content
)

# Write back
with open('contrib/oran-interface/model/ric-control-message.h', 'w') as f:
    f.write(content)

print("Successfully updated ric-control-message.h")

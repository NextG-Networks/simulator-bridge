#!/usr/bin/env python3
"""
Script to restore ControlCommand struct and m_commands member to header file
"""

# Read the header file
with open('contrib/oran-interface/model/ric-control-message.h', 'r') as f:
    content = f.read()

# Find where to insert ControlCommand struct (before ApplySimpleCommand)
struct_def = '''    struct ControlCommand {
        std::string type;
        uint16_t targetId;
        double value;
    };

'''

# Find where to insert m_commands (after m_requestType)
member_def = '''    std::vector<ControlCommand> m_commands;
'''

# Insert struct before "static void ApplySimpleCommand"
content = content.replace('    static void ApplySimpleCommand', struct_def + '    static void ApplySimpleCommand')

# Insert member after "ControlMessageRequestIdType m_requestType;"
content = content.replace('    ControlMessageRequestIdType m_requestType;', '    ControlMessageRequestIdType m_requestType;\n' + member_def)

# Write back
with open('contrib/oran-interface/model/ric-control-message.h', 'w') as f:
    f.write(content)

print("Successfully restored ControlCommand struct and m_commands member")

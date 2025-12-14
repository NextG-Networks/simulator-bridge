import sys
import re

file_path = '/home/hybrid/proj/ns-3-mmwave-oran/contrib/oran-interface/model/ric-control-message.cc'

with open(file_path, 'r') as f:
    content = f.read()

# 1. Remove includes
includes_to_remove = [
    '#include <ns3/mmwave-enb-net-device.h>',
    '#include <ns3/mmwave-enb-phy.h>',
    '#include <ns3/mmwave-flex-tti-mac-scheduler.h>',
    '#include <ns3/node-list.h>',
    '#include <ns3/double.h>'
]
for inc in includes_to_remove:
    content = content.replace(inc, '')

# Add new includes
if '#include "ric-control-message.h"' in content:
    content = content.replace('#include "ric-control-message.h"', '#include "ric-control-message.h"\n#include <ns3/log.h>\n#include <ns3/simulator.h>\n#include <iostream>\n#include <sstream>\n#include <vector>')

# 2. Replace ApplySimpleCommand with ParseSimpleCommand
# Try different signatures
start_markers = [
    'void\nRicControlMessage::ApplySimpleCommand(const std::string& json)',
    'void RicControlMessage::ApplySimpleCommand(const std::string& json)',
    'void\nRicControlMessage::ApplySimpleCommand (const std::string& json)',
    'void RicControlMessage::ApplySimpleCommand (const std::string& json)'
]

start_pos = -1
for marker in start_markers:
    pos = content.find(marker)
    if pos != -1:
        start_pos = pos
        break

if start_pos != -1:
    # Find the end of the function. It ends before the next function definition.
    end_markers = [
        'void\nRicControlMessage::DecodeRicControlMessage',
        'void RicControlMessage::DecodeRicControlMessage',
        'std::string\nRicControlMessage::GetSecondaryCellIdHO',
        'std::string RicControlMessage::GetSecondaryCellIdHO'
    ]
    
    end_pos = -1
    for marker in end_markers:
        pos = content.find(marker)
        if pos != -1:
            end_pos = pos
            break
            
    if end_pos != -1:
        # Construct new function
        new_func = """void
RicControlMessage::ParseSimpleCommand(const std::string& json)
{
    NS_LOG_FUNCTION(this << json);
    
    std::string command;
    uint16_t nodeId = 0;
    double value = 0.0;
    
    // Extract command
    size_t cmdPos = json.find("\\"command\\"");
    if (cmdPos != std::string::npos) {
        size_t valStart = json.find(":", cmdPos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);
        
        std::string cmdStr = json.substr(valStart, valEnd - valStart);
        cmdStr.erase(0, cmdStr.find_first_not_of(" \\"\\t\\r\\n"));
        cmdStr.erase(cmdStr.find_last_not_of(" \\"\\t\\r\\n") + 1);
        command = cmdStr;
    }
    
    // Extract nodeId
    size_t nodePos = json.find("\\"nodeId\\"");
    if (nodePos != std::string::npos) {
        size_t valStart = json.find(":", nodePos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);
        
        std::string valStr = json.substr(valStart, valEnd - valStart);
        try {
            nodeId = (uint16_t)std::stoi(valStr);
        } catch (...) {}
    }
    
    // Extract value
    size_t valPos = json.find("\\"txPower\\"");
    if (valPos == std::string::npos) valPos = json.find("\\"mcs\\"");
    
    if (valPos != std::string::npos) {
        size_t valStart = json.find(":", valPos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);
        
        std::string valStr = json.substr(valStart, valEnd - valStart);
        try {
            value = std::stod(valStr);
        } catch (...) {}
    }
    
    if (!command.empty()) {
        ControlCommand cmd;
        cmd.type = command;
        cmd.targetId = nodeId;
        cmd.value = value;
        m_commands.push_back(cmd);
        NS_LOG_INFO("Parsed command: " << command << " for node " << nodeId << " value " << value);
    }
}

"""
        content = content[:start_pos] + new_func + content[end_pos:]
    else:
        print("End marker not found")
else:
    print("Start marker not found")

# 3. Replace call site
content = content.replace('ApplySimpleCommand(ascii);', 'ParseSimpleCommand(ascii);')

with open(file_path, 'w') as f:
    f.write(content)

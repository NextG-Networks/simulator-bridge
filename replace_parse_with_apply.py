#!/usr/bin/env python3
"""
Script to replace ParseSimpleCommand with ApplySimpleCommand in ric-control-message.cc
"""

import re

# Read the current file
with open('contrib/oran-interface/model/ric-control-message.cc', 'r') as f:
    content = f.read()

# Read the working ApplySimpleCommand implementation
with open('/home/hybrid/proj/temp/ric-control-message-working.cc', 'r') as f:
    working_content = f.read()

# Extract ApplySimpleCommand from working file (lines 88-445)
apply_simple_command = '''// Command executor: supports {"cmd":"move-enb","node":<id>,"x":<X>,"y":<Y>}
void
RicControlMessage::ApplySimpleCommand(const std::string& json)
{
    // --- tiny field extractors (no JSON lib needed) ---
    auto FindNumber = [](const std::string& s, const char* key, double& outVal) -> bool {
        size_t k = s.find(key);
        if (k == std::string::npos) return false;
        k = s.find(':', k);
        if (k == std::string::npos) return false;
        while (k+1 < s.size() && s[k+1] == ' ') ++k;
        char* endp = nullptr;
        outVal = strtod(s.c_str() + k + 1, &endp);
        return endp != (s.c_str() + k + 1);
    };
    auto FindUint = [&](const std::string& s, const char* key, uint32_t& outVal) -> bool {
        double v;
        if (!FindNumber(s, key, v) || v < 0) return false;
        outVal = static_cast<uint32_t>(v + 0.5);
        return true;
    };
    auto FindString = [](const std::string& s, const char* key, std::string& out) -> bool {
        size_t k = s.find(key);
        if (k == std::string::npos) return false;
        k = s.find(':', k);
        if (k == std::string::npos) return false;
        k = s.find('"', k);
        if (k == std::string::npos) return false;
        size_t e = s.find('"', k+1);
        if (e == std::string::npos) return false;
        out.assign(s.begin() + k + 1, s.begin() + e);
        return true;
    };

    fprintf(stderr, "[RicControlMessage] ApplySimpleCommand: Input JSON = '%s' (len=%zu)\\n", json.c_str(), json.size());
    fflush(stderr);
    
    std::string cmd;
    if (!FindString(json, "\\"cmd\\"", cmd)) {
        fprintf(stderr, "[RicControlMessage] ERROR: No 'cmd' found in control JSON: '%s'\\n", json.c_str());
        fflush(stderr);
        return;
    }
    
    fprintf(stderr, "[RicControlMessage] Extracted cmd = '%s'\\n", cmd.c_str());
    fflush(stderr);

    

    if (cmd == "stop") {
        ns3::Simulator::ScheduleNow([]() {
            fprintf(stderr, "[RicControlMessage] stop: Stopping simulator now\\n");
            ns3::Simulator::Stop();
        });
        return;
    }



    if (cmd == "set-mcs") {
        uint32_t nodeId = 0;
        double mcsValue = 0.0;
        
        if (!FindUint(json, "\\"node\\"", nodeId) || !FindNumber(json, "\\"mcs\\"", mcsValue)) {
            fprintf(stderr, "[RicControlMessage] set-mcs requires node and mcs values\\n");
            return;
        }
        
        int mcs = static_cast<int>(mcsValue);
        if (mcs < 0 || mcs > 28) {
            fprintf(stderr, "[RicControlMessage] set-mcs: MCS must be between 0 and 28, got %d\\n", mcs);
            return;
        }
        
        ns3::Simulator::ScheduleNow([nodeId, mcs]() {
            using namespace ns3;
            
            // Find the mmWave eNB device for this node
            if (nodeId >= NodeList::GetNNodes()) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u does not exist\\n", nodeId);
                return;
            }
            
            Ptr<Node> n = NodeList::GetNode(nodeId);
            if (!n) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u not found\\n", nodeId);
                return;
            }
            
            // Find MmWaveEnbNetDevice
            Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
            for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
                enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
                if (enbDev) break;
            }
            
            if (!enbDev) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u has no MmWaveEnbNetDevice\\n", nodeId);
                return;
            }
            
            // Set MCS via MAC layer
            Ptr<mmwave::MmWaveEnbMac> mac = enbDev->GetMac();
            if (mac) {
                mac->SetMcs(mcs);
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u MCS set to %d\\n", nodeId, mcs);
                fflush(stderr);
            } else {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u has no MAC layer\\n", nodeId);
                fflush(stderr);
            }
        });
        return;
    }
    
    if (cmd == "set-bandwidth") {
        uint32_t nodeId = 0;
        double bwValue = 0.0;
        
        // nodeId is optional - if 0 or not provided, search all nodes
        bool hasNodeId = FindUint(json, "\\"node\\"", nodeId);
        if (!FindNumber(json, "\\"bandwidth\\"", bwValue)) {
            fprintf(stderr, "[RicControlMessage] set-bandwidth requires bandwidth value\\n");
            return;
        }
        
        uint8_t bandwidth = static_cast<uint8_t>(bwValue);
        
        ns3::Simulator::ScheduleNow([hasNodeId, nodeId, bandwidth]() {
            using namespace ns3;
            
            Ptr<mmwave::MmWaveEnbNetDevice> enbDev = nullptr;
            uint32_t foundNodeId = 0;
            
            if (hasNodeId && nodeId > 0) {
                // Try the specified node first
                if (nodeId < NodeList::GetNNodes()) {
                    Ptr<Node> n = NodeList::GetNode(nodeId);
                    if (n) {
                        for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
                            enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
                            if (enbDev) {
                                foundNodeId = nodeId;
                                break;
                            }
                        }
                    }
                }
            }
            
            // If not found and nodeId was specified, or if nodeId wasn't specified, search all nodes
            if (!enbDev) {
                for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
                    Ptr<Node> n = NodeList::GetNode(i);
                    if (!n) continue;
                    
                    for (uint32_t j = 0; j < n->GetNDevices(); ++j) {
                        enbDev = n->GetDevice(j)->GetObject<mmwave::MmWaveEnbNetDevice>();
                        if (enbDev) {
                            foundNodeId = i;
                            break;
                        }
                    }
                    if (enbDev) break;
                }
            }
            
            if (!enbDev) {
                fprintf(stderr, "[RicControlMessage] set-bandwidth: no MmWaveEnbNetDevice found in any node\\n");
                return;
            }
            
            // Set bandwidth
            enbDev->SetBandwidth(bandwidth);
            uint8_t bandwidth2 = enbDev->GetBandwidth();
            fprintf(stderr, "[RicControlMessage] set-bandwidth: node %u bandwidth set to %u (confirmed %u)\\n", foundNodeId, bandwidth, bandwidth2);
            fflush(stderr);
        });
        return;
    }

    if (cmd == "set-enb-txpower") {
        uint32_t nodeId = 0;
        double txPowerDbm = 0.0;

        if (!FindUint(json, "\\"node\\"", nodeId) ||
            !FindNumber(json, "\\"txPowerDbm\\"", txPowerDbm)) {
            fprintf(stderr,
                "[RicControlMessage] set-enb-txpower requires node and txPowerDbm\\n");
            return;
        }

        ns3::Simulator::ScheduleNow([nodeId, txPowerDbm]() {
            using namespace ns3;

            if (nodeId >= NodeList::GetNNodes()) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u does not exist\\n",
                    nodeId);
                return;
            }

            Ptr<Node> n = NodeList::GetNode(nodeId);
            if (!n) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u not found\\n",
                    nodeId);
                return;
            }

            Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
            for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
                enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
                if (enbDev) break;
            }

            if (!enbDev) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u has no MmWaveEnbNetDevice\\n",
                    nodeId);
                return;
            }

            Ptr<mmwave::MmWaveEnbPhy> phy = enbDev->GetPhy();
            if (!phy) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u has no PHY\\n",
                    nodeId);
                return;
            }

            phy->SetTxPower(txPowerDbm);
            fprintf(stderr,
                "[RicControlMessage] set-enb-txpower: node %u TxPower set to %.2f dBm\\n",
                nodeId, txPowerDbm);
            fflush(stderr);
        });
        return;
    }

    fprintf(stderr, "[RicControlMessage] Unknown cmd='%s' (valid commands: stop, set-mcs, set-bandwidth, set-enb-txpower)\\n", cmd.c_str());
    fflush(stderr);
}

'''

# Find and replace ParseSimpleCommand with ApplySimpleCommand
# First, find the start of ParseSimpleCommand
parse_start = content.find('RicControlMessage::ParseSimpleCommand(const std::string& json)')
if parse_start == -1:
    print("ERROR: Could not find ParseSimpleCommand")
    exit(1)

# Find the end of ParseSimpleCommand (the closing brace before DecodeRicControlMessage)
# Look for the function that comes after ParseSimpleCommand
decode_start = content.find('RicControlMessage::DecodeRicControlMessage(E2AP_PDU_t* pdu)', parse_start)
if decode_start == -1:
    print("ERROR: Could not find DecodeRicControlMessage")
    exit(1)

# Find the start of the ParseSimpleCommand function (go back to find "void")
func_start = content.rfind('void\n', 0, parse_start)
if func_start == -1:
    func_start = content.rfind('void ', 0, parse_start)

# Find the end of ParseSimpleCommand (the closing brace before DecodeRicControlMessage)
func_end = content.rfind('}', func_start, decode_start)

# Replace ParseSimpleCommand with ApplySimpleCommand
new_content = content[:func_start] + apply_simple_command + '\n\n' + content[decode_start:]

# Also need to change ParseSimpleCommand call to ApplySimpleCommand in DecodeRicControlMessage
new_content = new_content.replace('ParseSimpleCommand(ascii);', 'ApplySimpleCommand(ascii);')

# Write the modified content
with open('contrib/oran-interface/model/ric-control-message.cc', 'w') as f:
    f.write(new_content)

print("Successfully replaced ParseSimpleCommand with ApplySimpleCommand")

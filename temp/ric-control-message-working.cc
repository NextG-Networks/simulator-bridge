/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Northeastern University
 * Copyright (c) 2022 Sapienza, University of Rome
 * Copyright (c) 2022 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Andrea Lacava <thecave003@gmail.com>
 *		   Tommaso Zugno <tommasozugno@gmail.com>
 *		   Michele Polese <michele.polese@gmail.com>
 */
 
#include <ns3/ric-control-message.h>
#include <ns3/asn1c-types.h>
#include <ns3/log.h>
#include <bitset>
#include <iomanip>   // at top of file for std::setw, std::setfill, std::hex
#include <sstream>
#include "ns3/node-list.h"
#include "ns3/node.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"
#include "ns3/vector.h"

#include "control-gateway.h"
#include "ric-control-message.h"
#include <ns3/mmwave-enb-net-device.h>
#include <ns3/mmwave-enb-mac.h>
#include <ns3/onoff-application.h>
#include <ns3/data-rate.h>
//#include <ns3/time.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RicControlMessage");


// Very small parser helpers
static bool FindNumber(const std::string& s, const char* key, double& outVal)
{
    size_t k = s.find(key);
    if (k == std::string::npos) return false;
    k = s.find(':', k);
    if (k == std::string::npos) return false;
    // skip spaces
    while (k+1 < s.size() && (s[k+1] == ' ')) ++k;
    char* endp = nullptr;
    outVal = strtod(s.c_str() + k + 1, &endp);
    return endp != (s.c_str() + k + 1);
}

static bool FindUint(const std::string& s, const char* key, uint32_t& outVal)
{
    double v;
    if (!FindNumber(s, key, v)) return false;
    if (v < 0) return false;
    outVal = static_cast<uint32_t>(v + 0.5);
    return true;
}

static bool FindString(const std::string& s, const char* key, std::string& out)
{
    size_t k = s.find(key);
    if (k == std::string::npos) return false;
    k = s.find(':', k);
    if (k == std::string::npos) return false;
    k = s.find('"', k);
    if (k == std::string::npos) return false;
    size_t e = s.find('"', k+1);
    if (e == std::string::npos) return false;
    out.assign(s.begin()+k+1, s.begin()+e);
    return true;
}

// Command executor: supports {"cmd":"move-enb","node":<id>,"x":<X>,"y":<Y>}
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

    fprintf(stderr, "[RicControlMessage] ApplySimpleCommand: Input JSON = '%s' (len=%zu)\n", json.c_str(), json.size());
    fflush(stderr);
    
    std::string cmd;
    if (!FindString(json, "\"cmd\"", cmd)) {
        fprintf(stderr, "[RicControlMessage] ERROR: No 'cmd' found in control JSON: '%s'\n", json.c_str());
        fflush(stderr);
        return;
    }
    
    fprintf(stderr, "[RicControlMessage] Extracted cmd = '%s'\n", cmd.c_str());
    fflush(stderr);

    

    if (cmd == "stop") {
        ns3::Simulator::ScheduleNow([]() {
            fprintf(stderr, "[RicControlMessage] stop: Stopping simulator now\n");
            ns3::Simulator::Stop();
        });
        return;
    }



    if (cmd == "set-mcs") {
        uint32_t nodeId = 0;
        double mcsValue = 0.0;
        
        if (!FindUint(json, "\"node\"", nodeId) || !FindNumber(json, "\"mcs\"", mcsValue)) {
            fprintf(stderr, "[RicControlMessage] set-mcs requires node and mcs values\n");
            return;
        }
        
        int mcs = static_cast<int>(mcsValue);
        if (mcs < 0 || mcs > 28) {
            fprintf(stderr, "[RicControlMessage] set-mcs: MCS must be between 0 and 28, got %d\n", mcs);
            return;
        }
        
        ns3::Simulator::ScheduleNow([nodeId, mcs]() {
            using namespace ns3;
            
            // Find the mmWave eNB device for this node
            if (nodeId >= NodeList::GetNNodes()) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u does not exist\n", nodeId);
                return;
            }
            
            Ptr<Node> n = NodeList::GetNode(nodeId);
            if (!n) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u not found\n", nodeId);
                return;
            }
            
            // Find MmWaveEnbNetDevice
            Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
            for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
                enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
                if (enbDev) break;
            }
            
            if (!enbDev) {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u has no MmWaveEnbNetDevice\n", nodeId);
                return;
            }
            
            // Set MCS via MAC layer
            Ptr<mmwave::MmWaveEnbMac> mac = enbDev->GetMac();
            if (mac) {
                mac->SetMcs(mcs);
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u MCS set to %d\n", nodeId, mcs);
                fflush(stderr);
            } else {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u has no MAC layer\n", nodeId);
                fflush(stderr);
            }
        });
        return;
    }
    
    if (cmd == "set-bandwidth") {
        uint32_t nodeId = 0;
        double bwValue = 0.0;
        
        // nodeId is optional - if 0 or not provided, search all nodes
        bool hasNodeId = FindUint(json, "\"node\"", nodeId);
        if (!FindNumber(json, "\"bandwidth\"", bwValue)) {
            fprintf(stderr, "[RicControlMessage] set-bandwidth requires bandwidth value\n");
            return;
        }
        
        uint8_t bandwidth = static_cast<uint8_t>(bwValue);
        // if (bandwidth == 0 || bandwidth > 255) {
        //     fprintf(stderr, "[RicControlMessage] set-bandwidth: bandwidth must be between 1 and 255, got %u\n", bandwidth);
        //     return;
        // }
        
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
                fprintf(stderr, "[RicControlMessage] set-bandwidth: no MmWaveEnbNetDevice found in any node\n");
                return;
            }
            
            // Set bandwidth
            enbDev->SetBandwidth(bandwidth);
            uint8_t bandwidth2 = enbDev->GetBandwidth();
            fprintf(stderr, "[RicControlMessage] set-bandwidth: node %u bandwidth set to %u (confirmed %u)\n", foundNodeId, bandwidth, bandwidth2);
        });
        return;
    }
    if (cmd == "set-flow-rate") {
        uint32_t nodeId = UINT32_MAX;  // Use max as "not specified"
        uint32_t appIndex = 0;
        double rateMbps = 0.0;
    
        // node is optional - if not specified, we'll search all nodes
        FindUint(json, "\"node\"", nodeId);
        if (!FindUint(json, "\"app\"", appIndex) ||
            !FindNumber(json, "\"rateMbps\"", rateMbps)) {
            fprintf(stderr,
                "[RicControlMessage] set-flow-rate requires app and rateMbps (node is optional)\n");
            return;
        }
    
        if (rateMbps <= 0.0) {
            fprintf(stderr,
                "[RicControlMessage] set-flow-rate: rateMbps must be > 0, got %f\n",
                rateMbps);
            return;
        }
    
        ns3::Simulator::ScheduleNow([nodeId, appIndex, rateMbps]() {
            using namespace ns3;
    
            Ptr<OnOffApplication> onoffApp = nullptr;
            uint32_t foundNodeId = nodeId;
            uint32_t foundAppIndex = appIndex;
            
            // If node is specified, try that node first
            if (nodeId != UINT32_MAX && nodeId < NodeList::GetNNodes()) {
                Ptr<Node> n = NodeList::GetNode(nodeId);
                if (n) {
                    // First try the specified app index on the specified node
                    if (appIndex < n->GetNApplications()) {
                        Ptr<Application> app = n->GetApplication(appIndex);
                        onoffApp = DynamicCast<OnOffApplication>(app);
                    }
                    
                    // If not found at specified index, search all applications on this node
                    if (!onoffApp) {
                        for (uint32_t i = 0; i < n->GetNApplications(); ++i) {
                            Ptr<Application> app = n->GetApplication(i);
                            Ptr<OnOffApplication> test = DynamicCast<OnOffApplication>(app);
                            if (test) {
                                onoffApp = test;
                                foundAppIndex = i;
                                break;
                            }
                        }
                    }
                }
            }
            
            // If still not found, search all nodes for OnOffApplication
            if (!onoffApp) {
                fprintf(stderr, "[RicControlMessage] set-flow-rate: Searching all nodes for OnOffApplication...\n");
                for (uint32_t nodeIdx = 0; nodeIdx < NodeList::GetNNodes(); ++nodeIdx) {
                    Ptr<Node> testNode = NodeList::GetNode(nodeIdx);
                    if (!testNode) continue;
                    
                    for (uint32_t i = 0; i < testNode->GetNApplications(); ++i) {
                        Ptr<Application> app = testNode->GetApplication(i);
                        Ptr<OnOffApplication> test = DynamicCast<OnOffApplication>(app);
                        if (test) {
                            onoffApp = test;
                            foundNodeId = nodeIdx;
                            foundAppIndex = i;
                            fprintf(stderr, "[RicControlMessage] set-flow-rate: Found OnOffApplication on node %u app %u\n",
                                    foundNodeId, foundAppIndex);
                            fflush(stderr);
                            break;
                        }
                    }
                    if (onoffApp) break;
                }
            }
            
            if (!onoffApp) {
                Ptr<Node> n = (nodeId != UINT32_MAX && nodeId < NodeList::GetNNodes()) 
                              ? NodeList::GetNode(nodeId) : nullptr;
                if (n) {
                    fprintf(stderr,
                        "[RicControlMessage] set-flow-rate: node %u has no OnOffApplication (total apps: %u). Available apps:\n",
                        nodeId, n->GetNApplications());
                    for (uint32_t i = 0; i < n->GetNApplications(); ++i) {
                        Ptr<Application> app = n->GetApplication(i);
                        fprintf(stderr, "  app[%u]: %s\n", i, app->GetInstanceTypeId().GetName().c_str());
                    }
                }
                fprintf(stderr, "[RicControlMessage] set-flow-rate: Searched all %u nodes, no OnOffApplication found.\n",
                        NodeList::GetNNodes());
                fflush(stderr);
                return;
            }
    
            // Set DataRate attribute for OnOffApplication
            // Format: "50Mbps" as a string
            std::ostringstream rateStr;
            rateStr << std::fixed << std::setprecision(2) << rateMbps << "Mbps";
            DataRate dataRate(rateStr.str());
            
            onoffApp->SetAttribute("DataRate", DataRateValue(dataRate));
            fprintf(stderr,
                "[RicControlMessage] set-flow-rate: node %u app %u rate set to %.2f Mbps",
                foundNodeId, foundAppIndex, rateMbps);
            if (foundNodeId != nodeId && nodeId != UINT32_MAX) {
                fprintf(stderr, " (searched node %u, found on node %u)", nodeId, foundNodeId);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        });
        return;
    }


    if (cmd == "set-enb-txpower") {
        uint32_t nodeId = 0;
        double txPowerDbm = 0.0;

        if (!FindUint(json, "\"node\"", nodeId) ||
            !FindNumber(json, "\"txPowerDbm\"", txPowerDbm)) {
            fprintf(stderr,
                "[RicControlMessage] set-enb-txpower requires node and txPowerDbm\n");
            return;
        }

        ns3::Simulator::ScheduleNow([nodeId, txPowerDbm]() {
            using namespace ns3;

            if (nodeId >= NodeList::GetNNodes()) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u does not exist\n",
                    nodeId);
                return;
            }

            Ptr<Node> n = NodeList::GetNode(nodeId);
            if (!n) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u not found\n",
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
                    "[RicControlMessage] set-enb-txpower: node %u has no MmWaveEnbNetDevice\n",
                    nodeId);
                return;
            }

            Ptr<mmwave::MmWaveEnbPhy> phy = enbDev->GetPhy();
            if (!phy) {
                fprintf(stderr,
                    "[RicControlMessage] set-enb-txpower: node %u has no PHY\n",
                    nodeId);
                return;
            }

            phy->SetTxPower(txPowerDbm);
            fprintf(stderr,
                "[RicControlMessage] set-enb-txpower: node %u TxPower set to %.2f dBm\n",
                nodeId, txPowerDbm);
        });
        return;
    }

    fprintf(stderr, "[RicControlMessage] Unknown cmd='%s' (valid commands: move-enb, stop, set-mcs, set-bandwidth)\n", cmd.c_str());
    fflush(stderr);
}




RicControlMessage::RicControlMessage (E2AP_PDU_t* pdu)
{
  DecodeRicControlMessage (pdu);
  NS_LOG_INFO ("End of RicControlMessage::RicControlMessage()");
}

RicControlMessage::~RicControlMessage ()
{

}

void
RicControlMessage::DecodeRicControlMessage(E2AP_PDU_t* pdu)
{
    if (!pdu) {
        fprintf(stderr, "[RicControlMessage] ERROR: pdu is null\n");
        return;
    }
    if (pdu->present != E2AP_PDU_PR_initiatingMessage) {
        fprintf(stderr, "[RicControlMessage] ERROR: PDU is not InitiatingMessage\n");
        return;
    }

    InitiatingMessage_t* mess = pdu->choice.initiatingMessage;
    if (mess->value.present != InitiatingMessage__value_PR_RICcontrolRequest) {
        fprintf(stderr, "[RicControlMessage] ERROR: InitiatingMessage is not RICcontrolRequest\n");
        return;
    }

    auto* request = (RICcontrolRequest_t*)&mess->value.choice.RICcontrolRequest;
    xer_fprint(stderr, &asn_DEF_RICcontrolRequest, request);

    const size_t ieCount = request->protocolIEs.list.count;
    fprintf(stderr, "[RicControlMessage] IE count = %zu\n", ieCount);
    if (ieCount == 0) {
        fprintf(stderr, "[RicControlMessage] ERROR: RICcontrolRequest has no IEs\n");
        return;
    }

    const uint8_t* rawCtrlMsgBuf = nullptr;
    size_t rawCtrlMsgLen = 0;

    for (size_t i = 0; i < ieCount; ++i) {
        RICcontrolRequest_IEs_t* ie = request->protocolIEs.list.array[i];
        switch (ie->value.present) {
        case RICcontrolRequest_IEs__value_PR_RICrequestID:
            m_ricRequestId = ie->value.choice.RICrequestID;
            fprintf(stderr, "[RicControlMessage] RICrequestID: requestor=%ld instance=%ld\n",
                    (long)m_ricRequestId.ricRequestorID, (long)m_ricRequestId.ricInstanceID);
            break;

        case RICcontrolRequest_IEs__value_PR_RANfunctionID:
            m_ranFunctionId = ie->value.choice.RANfunctionID;
            fprintf(stderr, "[RicControlMessage] RANfunctionID=%ld\n", (long)m_ranFunctionId);
            break;

        case RICcontrolRequest_IEs__value_PR_RICcontrolMessage:
            rawCtrlMsgBuf = ie->value.choice.RICcontrolMessage.buf;
            rawCtrlMsgLen = ie->value.choice.RICcontrolMessage.size;
            break;

        default:
            break;
        }
    }

    // RAW dump of the control message payload
    std::string ascii;
    if (rawCtrlMsgBuf && rawCtrlMsgLen > 0) {
        // Remove null terminator if present (the xApp sends null-terminated strings)
        size_t actualLen = rawCtrlMsgLen;
        if (rawCtrlMsgLen > 0 && rawCtrlMsgBuf[rawCtrlMsgLen - 1] == '\0') {
            actualLen = rawCtrlMsgLen - 1;
        }
        
        ascii.assign(reinterpret_cast<const char*>(rawCtrlMsgBuf), actualLen);
        
        // Trim any trailing whitespace or nulls
        while (!ascii.empty() && (ascii.back() == '\0' || ascii.back() == ' ' || ascii.back() == '\n' || ascii.back() == '\r')) {
            ascii.pop_back();
        }
        
        static const char* HEX = "0123456789abcdef";
        std::string hex; hex.reserve(rawCtrlMsgLen * 2);
        for (size_t i = 0; i < rawCtrlMsgLen; ++i) {
            unsigned char c = rawCtrlMsgBuf[i];
            hex.push_back(HEX[(c >> 4) & 0xF]);
            hex.push_back(HEX[c & 0xF]);
        }
        fprintf(stderr, "[RicControlMessage] RAW RICcontrolMessage len=%zu (actual=%zu) ascii='%s' hex=%s\n",
                rawCtrlMsgLen, ascii.size(), ascii.c_str(), hex.c_str());
        fflush(stderr);
    } else {
        fprintf(stderr, "[RicControlMessage] RAW RICcontrolMessage is empty or missing\n");
        fflush(stderr);
    }

    // Proof-of-concept: interpret a tiny JSON with "cmd" and apply it
    if (!ascii.empty()) {
        fprintf(stderr, "[RicControlMessage] Calling ApplySimpleCommand with: '%s'\n", ascii.c_str());
        fflush(stderr);
        ApplySimpleCommand(ascii);
    } else {
        fprintf(stderr, "[RicControlMessage] ERROR: Control message payload is empty, cannot apply command\n");
        fflush(stderr);
    }
}




std::string
RicControlMessage::GetSecondaryCellIdHO ()
{
  return m_secondaryCellId;
}

std::vector<RANParameterItem>
RicControlMessage::ExtractRANParametersFromControlMessage (
    E2SM_RC_ControlMessage_Format1_t *e2SmRcControlMessageFormat1)
{
  std::vector<RANParameterItem> ranParameterList;
  int count = e2SmRcControlMessageFormat1->ranParameters_List->list.count;
  for (int i = 0; i < count; i++)
    {
      RANParameter_Item_t *ranParameterItem =
          e2SmRcControlMessageFormat1->ranParameters_List->list.array[i];
      for (RANParameterItem extractedParameter :
           RANParameterItem::ExtractRANParametersFromRANParameter (ranParameterItem))
        {
          ranParameterList.push_back (extractedParameter);
        }
    }

  return ranParameterList;
}

} // namespace ns3

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

    std::string cmd;
    if (!FindString(json, "\"cmd\"", cmd)) {
        fprintf(stderr, "[RicControlMessage] No 'cmd' in control JSON\n");
        return;
    }

    if (cmd == "move-enb") {
        uint32_t nodeId = 0;

        // Incremental move: prefer dx/dy/dz; fall back to x/y/z as deltas.
        double dx = 0.0, dy = 0.0, dz = 0.0;
        bool okNode = FindUint(json, "\"node\"", nodeId);

        bool hasDx = FindNumber(json, "\"dx\"", dx);
        bool hasDy = FindNumber(json, "\"dy\"", dy);
        bool hasDz = FindNumber(json, "\"dz\"", dz);

        double tmp;
        if (!hasDx && FindNumber(json, "\"x\"", tmp)) { dx = tmp; hasDx = true; }
        if (!hasDy && FindNumber(json, "\"y\"", tmp)) { dy = tmp; hasDy = true; }
        if (!hasDz && FindNumber(json, "\"z\"", tmp)) { dz = tmp; hasDz = true; }

        if (!(okNode && hasDx && hasDy)) {
            fprintf(stderr, "[RicControlMessage] move-enb requires node and dx/dy (or x/y) deltas\n");
            return;
        }

        // Do the move on the simulator thread
        ns3::Simulator::ScheduleNow([nodeId, dx, dy, dz]() {
            using namespace ns3;

            Ptr<MobilityModel> mm;
            uint32_t chosenId = nodeId;

            // Try requested node first
            if (nodeId < NodeList::GetNNodes()) {
                Ptr<Node> n = NodeList::GetNode(nodeId);
                mm = n->GetObject<MobilityModel>();
            }

            // Fallback: first node that has a MobilityModel
            if (!mm) {
                for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
                    Ptr<Node> n = NodeList::GetNode(i);
                    Ptr<MobilityModel> test = n->GetObject<MobilityModel>();
                    if (test) { mm = test; chosenId = i; break; }
                }
            }

            if (!mm) {
                fprintf(stderr, "[RicControlMessage] move-enb: no node with MobilityModel found\n");
                return;
            }

            Vector before = mm->GetPosition();
            Vector after  = before;
            after.x += dx;
            after.y += dy;
            after.z += dz;

            mm->SetPosition(after);

            if (chosenId == nodeId) {
                fprintf(stderr,
                        "[RicControlMessage] move-enb (increment): node %u "
                        "BEFORE=(%.3f, %.3f, %.3f)  DELTA=(%.3f, %.3f, %.3f)  AFTER=(%.3f, %.3f, %.3f)\n",
                        nodeId, before.x, before.y, before.z, dx, dy, dz, after.x, after.y, after.z);
            } else {
                fprintf(stderr,
                        "[RicControlMessage] move-enb (increment): requested node %u had no MobilityModel; "
                        "moved node %u instead  BEFORE=(%.3f, %.3f, %.3f)  DELTA=(%.3f, %.3f, %.3f)  AFTER=(%.3f, %.3f, %.3f)\n",
                        nodeId, chosenId, before.x, before.y, before.z, dx, dy, dz, after.x, after.y, after.z);
            }
        });
        return;
    }

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
            } else {
                fprintf(stderr, "[RicControlMessage] set-mcs: node %u has no MAC layer\n", nodeId);
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

    fprintf(stderr, "[RicControlMessage] Unknown cmd='%s'\n", cmd.c_str());
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
    fprintf(stderr, "[RicControlMessage] DecodeRicControlMessage CALLED\n");

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
        ascii.assign(reinterpret_cast<const char*>(rawCtrlMsgBuf),
                     static_cast<size_t>(rawCtrlMsgLen));
        static const char* HEX = "0123456789abcdef";
        std::string hex; hex.reserve(rawCtrlMsgLen * 2);
        for (size_t i = 0; i < rawCtrlMsgLen; ++i) {
            unsigned char c = rawCtrlMsgBuf[i];
            hex.push_back(HEX[(c >> 4) & 0xF]);
            hex.push_back(HEX[c & 0xF]);
        }
        fprintf(stderr, "[RicControlMessage] RAW RICcontrolMessage len=%zu ascii='%s' hex=%s\n",
                rawCtrlMsgLen, ascii.c_str(), hex.c_str());
    } else {
        fprintf(stderr, "[RicControlMessage] RAW RICcontrolMessage is empty or missing\n");
    }

    // Proof-of-concept: interpret a tiny JSON with "cmd" and apply it
    if (!ascii.empty()) {
        ApplySimpleCommand(ascii);
    }

    fprintf(stderr, "[RicControlMessage] DecodeRicControlMessage END\n");
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

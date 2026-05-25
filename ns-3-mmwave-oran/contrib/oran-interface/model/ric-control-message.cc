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
 
#include "ric-control-message.h"
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <ns3/asn1c-types.h>
#include <ns3/log.h>
#include <bitset>
#include <iomanip>   // at top of file for std::setw, std::setfill, std::hex
#include <sstream>
#include "ns3/node-list.h"
#include "ns3/node.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"
#include "ric-control-message.h"
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <iostream>
#include <sstream>
#include <vector>
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
RicControlMessage::ParseSimpleCommand(const std::string& json)
{
    NS_LOG_FUNCTION(this << json);
    
    std::string command;
    uint16_t nodeId = 0;
    double value = 0.0;
    std::string strValue;

    // Extract command
    size_t cmdPos = json.find("\"cmd\"");
    if (cmdPos != std::string::npos) {
        size_t valStart = json.find(":", cmdPos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);

        std::string cmdStr = json.substr(valStart, valEnd - valStart);
        cmdStr.erase(0, cmdStr.find_first_not_of(" \"\t\r\n"));
        cmdStr.erase(cmdStr.find_last_not_of(" \"\t\r\n") + 1);
        command = cmdStr;
    }

    // Extract nodeId
    size_t nodePos = json.find("\"node\"");
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
    size_t valPos = json.find("\"txPowerDbm\"");
    if (valPos == std::string::npos) valPos = json.find("\"mcs\"");
    if (valPos == std::string::npos) valPos = json.find("\"bandwidth\"");

    if (valPos != std::string::npos) {
        size_t valStart = json.find(":", valPos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);

        std::string valStr = json.substr(valStart, valEnd - valStart);
        try {
            value = std::stod(valStr);
        } catch (...) {}
    }

    // Extract string payload (e.g. chaos "event":"SPIKE")
    size_t evPos = json.find("\"event\"");
    if (evPos != std::string::npos) {
        size_t valStart = json.find(":", evPos) + 1;
        size_t valEnd = json.find(",", valStart);
        if (valEnd == std::string::npos) valEnd = json.find("}", valStart);

        std::string evStr = json.substr(valStart, valEnd - valStart);
        evStr.erase(0, evStr.find_first_not_of(" \"\t\r\n"));
        evStr.erase(evStr.find_last_not_of(" \"\t\r\n") + 1);
        strValue = evStr;
    }

    if (!command.empty()) {
        ControlCommand cmd;
        cmd.type = command;
        cmd.targetId = nodeId;
        cmd.value = value;
        cmd.strValue = strValue;
        m_commands.push_back(cmd);
        NS_LOG_INFO("Parsed command: " << command << " for node " << nodeId
                    << " value " << value << " strValue '" << strValue << "'");
    }
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
        ParseSimpleCommand(ascii);
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

RicControlMessage::RicControlMessage(E2AP_PDU_t* pdu) { DecodeRicControlMessage(pdu); }
RicControlMessage::~RicControlMessage() {}
} // namespace ns3

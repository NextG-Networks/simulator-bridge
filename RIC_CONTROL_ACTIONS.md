# RIC Control Actions - Implementation Guide

This document describes what control actions can be implemented in the RIC control message system. The flow is:
1. **AI** → sends JSON command to xApp
2. **xApp** → parses JSON, sends RIC message (E2SM-RC) to NS3
3. **NS3** → receives RIC message, applies command via `RicControlMessage::ApplySimpleCommand()`

---

## Currently Implemented Commands (in `ric-control-message.cc`)

These commands are **directly supported** in NS3 via `ApplySimpleCommand()`:

### 1. `move-enb` - Move Base Station
**JSON Format:**
```json
{"cmd":"move-enb","node":0,"dx":10.0,"dy":5.0,"dz":0.0}
```
or
```json
{"cmd":"move-enb","node":0,"x":100.0,"y":50.0,"z":0.0}
```

**Parameters:**
- `node`: Node ID (uint32)
- `dx`, `dy`, `dz`: Delta movement (double) - **preferred**
- `x`, `y`, `z`: Absolute position (double) - treated as deltas if dx/dy/dz not provided

**Implementation:** Moves the eNB node's `MobilityModel` position.

**Use Cases:**
- Mobility testing
- Coverage optimization
- Dynamic cell positioning

---

### 2. `stop` - Stop Simulation
**JSON Format:**
```json
{"cmd":"stop"}
```

**Parameters:** None

**Implementation:** Calls `Simulator::Stop()` immediately.

**Use Cases:**
- Emergency stop
- Test termination
- Controlled shutdown

---

### 3. `set-mcs` - Set Modulation and Coding Scheme
**JSON Format:**
```json
{"cmd":"set-mcs","node":0,"mcs":15}
```

**Parameters:**
- `node`: Node ID (uint32) - the eNB node
- `mcs`: MCS value (int, 0-28)

**Implementation:** 
- Finds `MmWaveEnbNetDevice` for the node
- Calls `GetMac()->SetMcs(mcs)`
- Sets MCS via scheduler SAP

**Use Cases:**
- Link adaptation control
- Testing different MCS values
- Performance optimization

---

### 4. `set-bandwidth` - Set Bandwidth
**JSON Format:**
```json
{"cmd":"set-bandwidth","node":0,"bandwidth":100}
```

**Parameters:**
- `node`: Node ID (uint32, optional - if 0 or missing, searches all nodes)
- `bandwidth`: Bandwidth in RBs (uint8)

**Implementation:**
- Finds `MmWaveEnbNetDevice` for the node
- Calls `SetBandwidth(bandwidth)`

**Use Cases:**
- Dynamic bandwidth allocation
- Load balancing
- Resource optimization

---

## xApp-Level Commands (via AI_CONTROL_API.md)

These commands are handled by the **xApp** and written to CSV files, then read by NS3:

### 5. `qos` - PRB Allocation
**JSON Format:**
```json
{"type":"qos","commands":[{"ueId":"111000000000001","percentage":0.7}]}
```

**Implementation:** xApp writes to `qos_actions.csv`, NS3 reads and applies via `SetUeQoS()`

---

### 6. `handover` / `ts` - Handover Control
**JSON Format:**
```json
{"type":"handover","commands":[{"imsi":"111000000000001","targetCellId":"1112"}]}
```

**Implementation:** xApp writes to `ts_actions_for_ns3.csv`, NS3 reads and calls `PerformHandoverToTargetCell()`

---

### 7. `energy` / `es` - Cell Energy Control
**JSON Format:**
```json
{"type":"energy","commands":[{"cellId":"1112","hoAllowed":0}]}
```

**Implementation:** xApp writes to `es_actions_for_ns3.csv`, NS3 reads and calls `SetSecondaryCellHandoverAllowedStatus()`

---

### 8. `set-enb-txpower` - eNB Transmit Power
**JSON Format:**
```json
{"type":"set-enb-txpower","commands":[{"cellId":"1112","dbm":43.0}]}
```

**Implementation:** xApp writes to `enb_txpower_actions.csv`, NS3 reads and sets PHY TX power

---

### 9. `set-ue-txpower` - UE Transmit Power
**JSON Format:**
```json
{"type":"set-ue-txpower","commands":[{"ueId":"111000000000001","dbm":23.0}]}
```

**Implementation:** xApp writes to `ue_txpower_actions.csv`, NS3 reads and sets UE PHY TX power

---

### 10. `set-cbr` - CBR Traffic Rate
**JSON Format:**
```json
{"type":"set-cbr","rate":"50Mbps","pktBytes":1200}
```

**Implementation:** xApp writes to `cbr_actions.csv`, NS3 reads and updates `OnOffApplication` data rate

---

### 11. `cap-ue-prb` - PRB Cap per UE
**JSON Format:**
```json
{"type":"cap-ue-prb","commands":[{"ueId":"111000000000001","maxPrb":10}]}
```

**Implementation:** xApp writes to `prb_cap_actions.csv`, NS3 reads (scheduler modification needed for full implementation)

---

## Additional Actions That Can Be Implemented

Based on available mmWave APIs, here are **new actions** you can add to `ApplySimpleCommand()`:

### 12. `set-enb-txpower-direct` - Direct eNB TX Power Control
**Proposed JSON:**
```json
{"cmd":"set-enb-txpower-direct","node":0,"dbm":43.0}
```

**Implementation:**
```cpp
if (cmd == "set-enb-txpower-direct") {
    uint32_t nodeId = 0;
    double txPower = 0.0;
    
    if (!FindUint(json, "\"node\"", nodeId) || !FindNumber(json, "\"dbm\"", txPower)) {
        fprintf(stderr, "[RicControlMessage] set-enb-txpower-direct requires node and dbm\n");
        return;
    }
    
    ns3::Simulator::ScheduleNow([nodeId, txPower]() {
        // Find MmWaveEnbNetDevice
        Ptr<Node> n = NodeList::GetNode(nodeId);
        if (!n) return;
        
        Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
        for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
            enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
            if (enbDev) break;
        }
        
        if (enbDev) {
            Ptr<mmwave::MmWaveEnbPhy> phy = enbDev->GetPhy();
            if (phy) {
                phy->SetTxPower(txPower);
                fprintf(stderr, "[RicControlMessage] set-enb-txpower-direct: node %u TX power set to %.2f dBm\n", 
                        nodeId, txPower);
            }
        }
    });
    return;
}
```

**Available API:** `MmWaveEnbPhy::SetTxPower(double pow)`

---

### 13. `set-ue-txpower-direct` - Direct UE TX Power Control
**Proposed JSON:**
```json
{"cmd":"set-ue-txpower-direct","imsi":"111000000000001","dbm":23.0}
```

**Implementation:** Find UE node by IMSI, get `MmWaveUePhy`, call `SetTxPower()`

**Available API:** `MmWaveUePhy::SetTxPower(double pow)`

---

### 14. `set-cell-state` - Cell State Control (ON/IDLE/SLEEP/OFF)
**Proposed JSON:**
```json
{"cmd":"set-cell-state","node":0,"state":"sleep"}
```

**Implementation:**
```cpp
if (cmd == "set-cell-state") {
    uint32_t nodeId = 0;
    std::string state;
    
    if (!FindUint(json, "\"node\"", nodeId) || !FindString(json, "\"state\"", state)) {
        fprintf(stderr, "[RicControlMessage] set-cell-state requires node and state\n");
        return;
    }
    
    ns3::Simulator::ScheduleNow([nodeId, state]() {
        // Find MmWaveEnbNetDevice
        Ptr<Node> n = NodeList::GetNode(nodeId);
        if (!n) return;
        
        Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
        for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
            enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
            if (enbDev) break;
        }
        
        if (enbDev) {
            mmwave::MmWaveEnbNetDevice::enumModeEnergyBs cellState;
            if (state == "on") cellState = mmwave::MmWaveEnbNetDevice::ON;
            else if (state == "idle") cellState = mmwave::MmWaveEnbNetDevice::Idle;
            else if (state == "sleep") cellState = mmwave::MmWaveEnbNetDevice::Sleep;
            else if (state == "off") cellState = mmwave::MmWaveEnbNetDevice::OFF;
            else {
                fprintf(stderr, "[RicControlMessage] set-cell-state: invalid state '%s'\n", state.c_str());
                return;
            }
            
            enbDev->SetCellState(cellState);
            fprintf(stderr, "[RicControlMessage] set-cell-state: node %u state set to %s\n", 
                    nodeId, state.c_str());
        }
    });
    return;
}
```

**Available API:** `MmWaveEnbNetDevice::SetCellState(enumModeEnergyBs value)`

**States:**
- `"on"` / `"idle"` → `ON` / `Idle` (value 1)
- `"sleep"` / `"off"` → `Sleep` / `OFF` (value 0)

---

### 15. `set-frequency` - Set Carrier Frequency
**Proposed JSON:**
```json
{"cmd":"set-frequency","node":0,"frequency":28e9}
```

**Implementation:** Find `MmWaveEnbNetDevice`, get PHY, set frequency via `MmWavePhyMacCommon::SetBandwidth()` or channel model

**Available API:** `MmWaveNetDevice::SetEarfcn(uint16_t earfcn)`, channel model `SetFrequency()`

---

### 16. `set-antenna` - Set Antenna Configuration
**Proposed JSON:**
```json
{"cmd":"set-antenna","node":0,"antennaId":0}
```

**Implementation:** Find `MmWaveComponentCarrier`, call `SetAntenna()`

**Available API:** `MmWaveComponentCarrier::SetAntenna(Ptr<PhasedArrayModel> antenna)`

**Note:** Requires creating/selecting antenna model first.

---

### 17. `set-scheduler` - Change Scheduler Type
**Proposed JSON:**
```json
{"cmd":"set-scheduler","node":0,"type":"pf"}
```

**Available Schedulers:**
- `"pf"` → `MmWaveFlexTtiPfMacScheduler` (Proportional Fair)
- `"maxrate"` → `MmWaveFlexTtiMaxRateMacScheduler`
- `"maxweight"` → `MmWaveFlexTtiMaxWeightMacScheduler`

**Note:** This may require more complex implementation as scheduler is set during initialization.

---

### 18. `set-mcs-per-ue` - Set MCS per UE (not global)
**Proposed JSON:**
```json
{"cmd":"set-mcs-per-ue","imsi":"111000000000001","mcs":15}
```

**Implementation:** Find UE, get MAC, set UE-specific MCS (requires scheduler modification to support per-UE MCS)

**Note:** Current `SetMcs()` is global for the eNB. Per-UE MCS would require scheduler changes.

---

## Implementation Pattern

To add a new command, follow this pattern in `ApplySimpleCommand()`:

```cpp
if (cmd == "your-command-name") {
    // 1. Parse parameters
    uint32_t nodeId = 0;
    double value = 0.0;
    
    if (!FindUint(json, "\"node\"", nodeId) || !FindNumber(json, "\"value\"", value)) {
        fprintf(stderr, "[RicControlMessage] your-command-name requires node and value\n");
        return;
    }
    
    // 2. Schedule on simulator thread
    ns3::Simulator::ScheduleNow([nodeId, value]() {
        using namespace ns3;
        
        // 3. Find the device/node
        Ptr<Node> n = NodeList::GetNode(nodeId);
        if (!n) {
            fprintf(stderr, "[RicControlMessage] your-command-name: node %u not found\n", nodeId);
            return;
        }
        
        // 4. Find the specific device (e.g., MmWaveEnbNetDevice)
        Ptr<mmwave::MmWaveEnbNetDevice> enbDev;
        for (uint32_t i = 0; i < n->GetNDevices(); ++i) {
            enbDev = n->GetDevice(i)->GetObject<mmwave::MmWaveEnbNetDevice>();
            if (enbDev) break;
        }
        
        if (!enbDev) {
            fprintf(stderr, "[RicControlMessage] your-command-name: no MmWaveEnbNetDevice found\n");
            return;
        }
        
        // 5. Apply the action
        // ... your implementation ...
        
        fprintf(stderr, "[RicControlMessage] your-command-name: applied successfully\n");
    });
    return;
}
```

---

## Summary

**Direct RIC Commands (4):**
- ✅ `move-enb` - Move base station
- ✅ `stop` - Stop simulation
- ✅ `set-mcs` - Set MCS
- ✅ `set-bandwidth` - Set bandwidth

**xApp-Level Commands (7):**
- ✅ `qos` - PRB allocation
- ✅ `handover` / `ts` - Handover control
- ✅ `energy` / `es` - Cell energy control
- ✅ `set-enb-txpower` - eNB TX power
- ✅ `set-ue-txpower` - UE TX power
- ✅ `set-cbr` - CBR traffic rate
- ⚠️ `cap-ue-prb` - PRB cap (partial)

**Recommended New Commands (6):**
- 🔨 `set-enb-txpower-direct` - Direct eNB TX power (simpler than xApp version)
- 🔨 `set-cell-state` - Cell state control (ON/IDLE/SLEEP/OFF)
- 🔨 `set-frequency` - Carrier frequency
- 🔨 `set-antenna` - Antenna configuration
- 🔨 `set-scheduler` - Scheduler type (complex)
- 🔨 `set-mcs-per-ue` - Per-UE MCS (requires scheduler changes)

---

## Next Steps

1. **Choose which commands to implement** based on your AI's needs
2. **Add them to `ApplySimpleCommand()`** following the pattern above
3. **Test with simple JSON** sent via RIC control message
4. **Update xApp** to send these commands if needed (or use direct RIC path)


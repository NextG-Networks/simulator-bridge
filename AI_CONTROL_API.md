# AI Control API - Current Implementation

This document describes the **control commands** that the AI can send to the xApp to optimize network performance in real-time.

## Overview

The AI communicates with the xApp via TCP on port **5001** (configurable via `AI_CONFIG_PORT` environment variable). The xApp receives JSON configuration commands and converts them to NS3 control files, which are then read and applied by the NS3 simulation.

**Protocol:**
- TCP connection to xApp on port 5001
- Length-prefixed JSON messages (4-byte big-endian length + JSON payload)
- One-way communication (AI → xApp → NS3)

---

## Supported Commands

### 1. QoS / PRB Allocation (`qos`)

**Purpose:** Control the percentage of PRBs (Physical Resource Blocks) allocated to specific UEs. This allows the AI to prioritize certain UEs or balance resource allocation.

**Request Format:**
```json
{
  "type": "qos",
  "commands": [
    {
      "ueId": "111000000000001",
      "percentage": 0.7
    },
    {
      "ueId": "111000000000002",
      "percentage": 0.5
    }
  ]
}
```

**Fields:**
- `type`: Must be `"qos"`
- `commands`: Array of QoS commands
  - `ueId`: IMSI string (format: `"111"` + `"000000000001"` = PLMN ID + UE number)
    - Example: `"111000000000001"` → RNTI 1, `"111000000000002"` → RNTI 2
  - `percentage`: Float between 0.0 and 1.0 (0% to 100% of available PRBs)

**How it works:**
- The xApp converts IMSI to RNTI automatically (removes PLMN prefix "111")
- NS3 reads `qos_actions.csv` periodically and applies PRB allocation via `SetUeQoS()`
- Changes take effect within the next E2 reporting period (typically 100ms)

**Example:**
```python
config = {
    "type": "qos",
    "commands": [
        {"ueId": "111000000000001", "percentage": 0.8},  # Give UE 1 (RNTI 1) 80% PRBs
        {"ueId": "111000000000002", "percentage": 0.3}   # Give UE 2 (RNTI 2) 30% PRBs
    ]
}
```

**Use Cases:**
- Prioritize high-priority UEs
- Balance load across UEs
- Optimize throughput based on KPI analysis
- Implement QoS differentiation

---

### 2. Handover Control (`handover` or `ts`)

**Purpose:** Trigger handovers of UEs between cells. Useful for load balancing, interference management, or mobility optimization.

**Request Format:**
```json
{
  "type": "handover",
  "commands": [
    {
      "imsi": "111000000000001",
      "targetCellId": "1112"
    },
    {
      "imsi": "111000000000002",
      "targetCellId": "1113"
    }
  ]
}
```

**Fields:**
- `type`: Must be `"handover"` or `"ts"` (traffic steering)
- `commands`: Array of handover commands
  - `imsi`: IMSI string (same format as QoS command)
  - `targetCellId`: Target cell ID as string (e.g., `"1112"`, `"1113"`)

**How it works:**
- NS3 reads `ts_actions_for_ns3.csv` periodically
- Calls `PerformHandoverToTargetCell(imsi, targetCellId)` for each command
- Handovers are executed immediately or scheduled based on NS3 configuration

**Example:**
```python
config = {
    "type": "handover",
    "commands": [
        {"imsi": "111000000000001", "targetCellId": "1112"},  # Move UE 1 to cell 1112
        {"imsi": "111000000000002", "targetCellId": "1113"}   # Move UE 2 to cell 1113
    ]
}
```

**Use Cases:**
- Load balancing between cells
- Interference mitigation
- Mobility optimization
- Energy efficiency (move UEs to more efficient cells)

---

### 3. Energy Efficiency / Cell Control (`energy` or `es`)

**Purpose:** Enable or disable handovers to specific cells. This allows the AI to turn cells on/off for energy saving or capacity management.

**Request Format:**
```json
{
  "type": "energy",
  "commands": [
    {
      "cellId": "1112",
      "hoAllowed": 0
    },
    {
      "cellId": "1113",
      "hoAllowed": 1
    }
  ]
}
```

**Fields:**
- `type`: Must be `"energy"` or `"es"` (energy saving)
- `commands`: Array of energy commands
  - `cellId`: Cell ID as string (e.g., `"1112"`, `"1113"`)
  - `hoAllowed`: Integer (0 = disable handovers to this cell, 1 = enable)

**How it works:**
- NS3 reads `es_actions_for_ns3.csv` periodically
- Calls `SetSecondaryCellHandoverAllowedStatus(cellId, hoAllowed)` for each command
- When `hoAllowed = 0`, the cell is effectively disabled for new handovers

**Example:**
```python
config = {
    "type": "energy",
    "commands": [
        {"cellId": "1112", "hoAllowed": 0},  # Disable cell 1112 (energy saving)
        {"cellId": "1113", "hoAllowed": 1}   # Enable cell 1113
    ]
}
```

**Use Cases:**
- Energy saving (disable underutilized cells)
- Capacity management
- Load balancing (disable cells that are overloaded)
- Maintenance mode

---

## Implementation Details

### IMSI to RNTI Conversion

The xApp automatically converts IMSI strings to RNTI values:
- IMSI format: `"111"` (PLMN ID) + `"000000000001"` (UE number)
- Conversion: Remove PLMN prefix → Extract UE number → Use as RNTI
- Example: `"111000000000001"` → RNTI `1`, `"111000000000002"` → RNTI `2`

**Important:** NS3 assigns RNTIs sequentially starting from 1, so the UE number in the IMSI directly maps to the RNTI.

### Control File Format

The xApp writes control commands to CSV files in `/tmp/ns3-control/` (shared via Docker volume):

1. **`qos_actions.csv`**: `timestamp,rnti,percentage`
2. **`ts_actions_for_ns3.csv`**: `timestamp,imsi,targetCellId`
3. **`es_actions_for_ns3.csv`**: `timestamp,cellId,hoAllowed`

NS3 reads these files periodically (every `e2Periodicity` seconds, typically 0.1s).

### Timing and Latency

- **Command Processing:** Immediate (xApp writes to CSV immediately)
- **NS3 Application:** Within next E2 period (typically 100ms)
- **Effect Visibility:** Changes appear in next KPI report (typically 100-200ms after command)

---

## Example: Complete AI Control Flow

```python
import socket
import json
import struct

def send_config(config_json, host="127.0.0.1", port=5001):
    """Send configuration to xApp"""
    if isinstance(config_json, dict):
        config_json = json.dumps(config_json)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
        data = config_json.encode('utf-8')
        length = struct.pack('>I', len(data))
        sock.sendall(length + data)
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        sock.close()

# Example 1: Optimize QoS based on KPI analysis
# (After receiving KPI showing UE 1 has low throughput)
qos_config = {
    "type": "qos",
    "commands": [
        {"ueId": "111000000000001", "percentage": 0.8}  # Increase PRB allocation
    ]
}
send_config(qos_config)

# Example 2: Load balancing via handover
# (After detecting cell 1112 is overloaded)
ho_config = {
    "type": "handover",
    "commands": [
        {"imsi": "111000000000001", "targetCellId": "1113"}  # Move to less loaded cell
    ]
}
send_config(ho_config)

# Example 3: Energy saving
# (After detecting low traffic period)
energy_config = {
    "type": "energy",
    "commands": [
        {"cellId": "1112", "hoAllowed": 0}  # Disable underutilized cell
    ]
}
send_config(energy_config)
```

---

## Current Limitations

1. **No Response/Confirmation:** The current implementation is one-way (AI → xApp). The AI does not receive confirmation that commands were applied.

2. **No Validation Feedback:** If a command fails (e.g., invalid RNTI, cell ID), the AI won't be notified. Monitor KPIs to verify effects.

3. **Batch Processing:** Commands are processed in batches. If you send multiple commands quickly, they may be applied together.

4. **IMSI Format:** Currently assumes IMSI format `"111" + "000000000001"`. If your simulation uses different IMSI formats, the conversion may need adjustment.

---

## Additional Commands (Newly Implemented)

### 4. eNB Transmit Power (`set-enb-txpower`)

**Purpose:** Adjust the transmit power of base stations (eNBs) in real-time. Useful for interference management, coverage optimization, and energy efficiency.

**Request Format:**
```json
{
  "type": "set-enb-txpower",
  "commands": [
    {
      "cellId": "1112",
      "dbm": 43.0
    },
    {
      "cellId": "1113",
      "dbm": 38.0
    }
  ]
}
```

**Fields:**
- `type`: Must be `"set-enb-txpower"`
- `commands`: Array of eNB TX power commands
  - `cellId`: Cell ID as string (e.g., `"1112"`, `"1113"`)
  - `dbm`: Transmit power in dBm (double, typically 20-46 dBm)

**How it works:**
- NS3 reads `enb_txpower_actions.csv` periodically
- Calls `GetPhy()->SetAttribute("TxPower", ...)` for the matching cell
- Changes take effect immediately

**Example:**
```python
config = {
    "type": "set-enb-txpower",
    "commands": [
        {"cellId": "1112", "dbm": 43.0},  # Increase power for cell 1112
        {"cellId": "1113", "dbm": 35.0}   # Decrease power for cell 1113
    ]
}
```

**Use Cases:**
- Interference mitigation (reduce power in dense deployments)
- Coverage optimization (increase power for edge UEs)
- Energy efficiency (reduce power during low traffic)

---

### 5. UE Transmit Power (`set-ue-txpower`)

**Purpose:** Adjust the transmit power of individual UEs. Useful for power control optimization and interference management.

**Request Format:**
```json
{
  "type": "set-ue-txpower",
  "commands": [
    {
      "ueId": "111000000000001",
      "dbm": 23.0
    },
    {
      "ueId": "111000000000002",
      "dbm": 20.0
    }
  ]
}
```

**Fields:**
- `type`: Must be `"set-ue-txpower"`
- `commands`: Array of UE TX power commands
  - `ueId`: IMSI string (same format as QoS command)
  - `dbm`: Transmit power in dBm (double, typically 10-23 dBm)

**How it works:**
- The xApp converts IMSI to RNTI automatically
- NS3 reads `ue_txpower_actions.csv` periodically
- Finds the UE node by IMSI and sets its PHY TX power
- Changes take effect immediately

**Example:**
```python
config = {
    "type": "set-ue-txpower",
    "commands": [
        {"ueId": "111000000000001", "dbm": 23.0},  # Max power for UE 1
        {"ueId": "111000000000002", "dbm": 15.0}   # Lower power for UE 2
    ]
}
```

**Use Cases:**
- Power control optimization
- Interference reduction
- Battery life optimization for mobile UEs

---

### 6. CBR Traffic Rate (`set-cbr`)

**Purpose:** Dynamically adjust the Constant Bit Rate (CBR) traffic generation rate. Useful for load testing, capacity planning, and traffic engineering.

**Request Format:**
```json
{
  "type": "set-cbr",
  "rate": "50Mbps",
  "pktBytes": 1200
}
```

**Fields:**
- `type`: Must be `"set-cbr"`
- `rate`: Data rate as string (e.g., `"50Mbps"`, `"1Gbps"`, `"100kb/s"`)
- `pktBytes`: Packet size in bytes (optional, unsigned integer)

**How it works:**
- NS3 reads `cbr_actions.csv` periodically
- Finds all `OnOffApplication` instances in the simulation
- Updates their `DataRate` and/or `PacketSize` attributes
- Changes take effect immediately

**Example:**
```python
config = {
    "type": "set-cbr",
    "rate": "50Mbps",      # Set data rate to 50 Mbps
    "pktBytes": 1200       # Set packet size to 1200 bytes
}
```

**Note:** This command updates **all** OnOffApplication instances in the simulation. For per-UE traffic control, you would need to modify the scenario code to track application instances per UE.

**Use Cases:**
- Load testing (gradually increase traffic)
- Capacity planning
- Traffic engineering experiments

---

### 7. PRB Cap (`cap-ue-prb`)

**Purpose:** Set a maximum PRB (Physical Resource Block) allocation cap per UE. This is different from QoS percentage - it sets an absolute maximum, while QoS sets a percentage of available PRBs.

**Request Format:**
```json
{
  "type": "cap-ue-prb",
  "commands": [
    {
      "ueId": "111000000000001",
      "maxPrb": 10
    },
    {
      "ueId": "111000000000002",
      "maxPrb": 5
    }
  ]
}
```

**Fields:**
- `type`: Must be `"cap-ue-prb"`
- `commands`: Array of PRB cap commands
  - `ueId`: IMSI string (same format as QoS command)
  - `maxPrb`: Maximum number of PRBs (unsigned integer, typically 1-100)

**How it works:**
- The xApp converts IMSI to RNTI automatically
- NS3 reads `prb_cap_actions.csv` periodically
- **Note:** Full implementation requires scheduler modifications. Currently, the command is parsed and logged, but the scheduler needs to be modified to respect these caps during PRB allocation.

**Example:**
```python
config = {
    "type": "cap-ue-prb",
    "commands": [
        {"ueId": "111000000000001", "maxPrb": 10},  # Cap UE 1 to 10 PRBs
        {"ueId": "111000000000002", "maxPrb": 5}    # Cap UE 2 to 5 PRBs
    ]
}
```

**Limitation:** This command is **partially implemented**. The xApp writes the commands correctly, and NS3 parses them, but the scheduler needs to be modified to actually enforce the caps. This requires changes to the MAC scheduler code (e.g., `PfFfMacScheduler`, `RrFfMacScheduler`) to check PRB caps before allocation.

**Use Cases:**
- Network slicing (limit resources per slice)
- Fairness enforcement
- Resource isolation

---

## Future Enhancements (Not Yet Implemented)

The following commands from `RuntimeControlAPI.md` are **not yet implemented**:

- `pin-ue-mcs`: Control MCS (Modulation and Coding Scheme) per UE
- `set-e2-periodicity`: Change reporting frequency
- `set-slice-weights`: Scheduler weights for network slicing

---

## Testing

Use the provided `ai_send_config_example.py` script to test commands:

```bash
python3 ai_send_config_example.py
```

Monitor the NS3 logs to see commands being applied:
```
[NS3-CTRL] Applying QoS: RNTI=1 percentage=0.7
[NS3-CTRL] Applying QoS: RNTI=2 percentage=0.5
```

Monitor the AI dummy server to see KPI changes:
```
📱 Per-UE Measurements (3 UEs):
UE ID           UEThpDl      
3030303031      7,776 (+1123)  # Throughput increased after QoS change
```

---

## Questions or Issues?

- Check xApp logs for config reception: `[AI-CONFIG] Received config: ...`
- Check NS3 logs for command application: `[NS3-CTRL] Applying QoS: ...`
- Verify control files exist: `cat /tmp/ns3-control/qos_actions.csv`
- Ensure Docker volume is mounted correctly


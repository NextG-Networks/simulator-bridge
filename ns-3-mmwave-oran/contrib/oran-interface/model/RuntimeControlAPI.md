# Runtime Control Command API

This document describes the **runtime control commands** exposed by the simulation control interface for integration with an external controller / xApp.

Each command:

- Uses a **JSON** payload.
- Is executed on the **ns-3 simulation thread** (via `Simulator::ScheduleNow`).
- Has defined **validation**, **side effects**, and **response format**.

---

## 1. Conventions

### 1.1 Command Format

External controller sends:

```json
{ "command": "<name>", ...payload... }
```

On receipt:

1. Parse JSON.
2. Validate required fields.
3. Run the handler on the ns-3 thread, e.g.:

```cpp
Simulator::ScheduleNow([=] {
    HandleCommand(parsedJson);
});
```

**Never** modify ns-3 objects from non-simulator threads.

### 1.2 Lookups

Recommended helper patterns:

- Node by ID:

  ```cpp
  Ptr<Node> node = NodeList::GetNode(nodeId);
  ```
- UE by IMSI:

  - Maintain `imsi -> Ptr<MmWaveUeNetDevice>` mapping at attach time, or
  - Iterate over all nodes/devices and match `GetImsi()`.

If lookup fails: do nothing and report error.

### 1.3 Responses

On success:

```json
{ "ok": true }
```

On failure:

```json
{
  "ok": false,
  "code": "BAD_REQUEST|NOT_FOUND|INVALID_STATE|UNSUPPORTED",
  "error": "Human readable message"
}
```

Rules:

- No partial updates on error.
- Idempotent where reasonable (reapplying same values yields same state).

### 1.4 Units

Unless specified otherwise:

- Power: **dBm**
- Frequency/Bandwidth: **Hz**
- Time: **s** or **ms** (indicated by field name)
- Rate: bps or string `"50Mbps"`, `"1Gbps"`
- Counts: unsigned integers

---

## 2. Core Runtime Commands

### 2.1 `pin-ue-mcs`

**Purpose**
Control the MCS used for a specific UE. Allows the controller to enforce a fixed MCS or revert to normal AMC.

**Request**

```json
{
  "command": "pin-ue-mcs",
  "ue": "<imsi>",
  "dlMcs": <int>,
  "ulMcs": <int>
}
```

**Semantics**

- `dlMcs >= 0`: use this MCS for downlink.
- `ulMcs >= 0`: use this MCS for uplink.
- `dlMcs < 0`: restore AMC for downlink.
- `ulMcs < 0`: restore AMC for uplink.

**Validation**

- IMSI must exist.
- MCS indices must be valid for the configured tables.
- On validation failure: no state change.

---

### 2.2 `cap-ue-prb`

**Purpose**
Limit the maximum number of PRBs assigned to specified UEs per TTI. Used for resource control, slicing experiments, or throttling.

**Request**

```json
{
  "command": "cap-ue-prb",
  "caps": [
    { "ue": "<imsi>", "maxPrb": <uint> },
    { "ue": "<imsi2>", "maxPrb": <uint> }
  ]
}
```

**Semantics**

- For each entry, store `maxPrb` as the cap for that UE.
- The scheduler must enforce `allocatedPrb(ue) <= maxPrb`.

**Validation**

- `maxPrb` must be non-negative.
- Unknown IMSIs should either be reported as `NOT_FOUND` or ignored with a clear response.

**Implementation Note**

Requires the MAC scheduler to consult these caps during allocation.

---

### 2.3 `set-cbr`

**Purpose**
Dynamically adjust demo CBR traffic characteristics.

**Request**

```json
{
  "command": "set-cbr",
  "rate": "50Mbps",
  "pktBytes": 1200
}
```

**Semantics**

- Update the configured CBR `OnOffApplication`:
  - `DataRate` ← `rate`
  - `PacketSize` ← `pktBytes`

**Validation**

- `rate` must be parseable.
- `pktBytes` must be a sensible positive value.
- If the relevant app is not found, return `NOT_FOUND`.

---

### 2.4 `set-e2-periodicity`

**Purpose**
Adjust the period of E2 / telemetry reporting used by the controller.

**Request**

```json
{
  "command": "set-e2-periodicity",
  "s": <double>
}
```

**Semantics**

- Update the reporting interval to `s` seconds, if supported by the implementation.

**Validation**

- `s` must be within a configured valid range (e.g. `0.05`–`5.0`).
- If not supported at runtime, return `UNSUPPORTED`.

---

### 2.5 `toggle-e2-report`

**Purpose**
Enable or disable specific categories of E2-like reports.

**Request**

Any subset of the following fields:

```json
{
  "command": "toggle-e2-report",
  "lte": true,
  "nr": true,
  "du": false,
  "cuUp": true,
  "cuCp": false
}
```

**Semantics**

- For each provided key, enable/disable that reporting domain.

**Validation**

- At least one recognized key must be present.
- Unrecognized keys should be ignored or reported as `BAD_REQUEST`.

---

### 2.6 `toggle-e2-filelog`

**Purpose**
Control whether E2-style messages are written to file.

**Request**

```json
{
  "command": "toggle-e2-filelog",
  "enabled": true
}
```

**Semantics**

- `enabled = true`: enable file logging.
- `enabled = false`: disable file logging.

**Validation**

- If file logging is not available, return `UNSUPPORTED`.

---

## 3. Optional / Implementation-Dependent Commands

The following commands are allowed only if the underlying implementation provides safe runtime setters.
If not implemented, they must respond:

```json
{ "ok": false, "code": "UNSUPPORTED", "error": "Not implemented" }
```

### 3.1 `set-enb-txpower`

```json
{
  "command": "set-enb-txpower",
  "dbm": <double>
}
```

- If supported: update gNB TX power via appropriate PHY method.
- Validate within a sane range.

### 3.2 `set-ue-txpower`

```json
{
  "command": "set-ue-txpower",
  "ue": "<imsi>",
  "dbm": <double>
}
```

- If supported: update UE TX power.

### 3.3 `set-slice-weights`

```json
{
  "command": "set-slice-weights",
  "weights": [
    { "ue": "<imsi>", "w": <double> }
  ]
}
```

- If supported: scheduler uses weights each TTI.

### 3.4 `set-drx`, `set-rrc-meas`, `force-ho`

- May be defined if RRC/HO logic supports clean runtime reconfiguration.

---

## 4. Unsupported Structural Changes

The control interface does not define commands for:

- Changing carrier frequency, bandwidth, numerology, pathloss, or channel models at runtime.
- Changing antenna configurations at runtime.
- Altering protocol stack structure (e.g., RLC mode, scheduler class) at runtime.
- Adding/removing nodes or modifying core topology at runtime.

Such parameters are expected to be configured in the scenario code before `Simulator::Run()`.

# AI KPI Priority for gNB Configuration Optimization

## Overview

This document identifies the most relevant KPIs for an AI to make educated decisions about gNB configuration changes. The KPIs are prioritized based on their importance for network optimization and the control actions available.

## Priority 1: Critical KPIs (Must Have)

### 1. **Signal Quality Metrics (RSRP, RSRQ, SINR)**
**Why Critical:**
- **Handover decisions**: AI needs to know when UEs should be handed over to better cells
- **Coverage optimization**: Identify coverage holes or interference issues
- **Power control**: Adjust TX power based on signal quality

**What to Monitor:**
- Serving cell RSRP/RSRQ/SINR per UE
- Neighbor cell RSRP/RSRQ/SINR per UE
- Cell-level average SINR

**Control Actions Enabled:**
- Handover triggers (when neighbor SINR > serving SINR + threshold)
- eNB/UE TX power adjustment (increase power if SINR is low)
- Cell on/off decisions (turn off cells with poor coverage)

**Current Status:**
- ✅ SINR available (from RRC measurements)
- ❌ RSRP/RSRQ NOT available (NS3 only sends SINR)

---

### 2. **Throughput (DL/UL)**
**Why Critical:**
- **QoS optimization**: Identify UEs that need more resources
- **Load balancing**: Distribute traffic across cells
- **Congestion detection**: Identify overloaded cells

**What to Monitor:**
- Per-UE DL/UL throughput (Mbps)
- Cell-level aggregate throughput
- Throughput distribution (min/max/mean)

**Control Actions Enabled:**
- PRB allocation (QoS) - give more PRBs to UEs with low throughput
- Handover - move UEs from congested cells
- Traffic shaping (CBR) - adjust traffic generation

**Current Status:**
- ✅ DL throughput available (`DRB.UEThpDl.UEID`)
- ❌ UL throughput NOT available (NS3 limitation)

---

### 3. **PRB Usage (Resource Utilization)**
**Why Critical:**
- **Load balancing**: Identify overloaded vs underutilized cells
- **QoS decisions**: Know how many resources are available
- **Energy efficiency**: Turn off cells with low utilization

**What to Monitor:**
- Per-UE PRB usage (DL/UL)
- Cell-level PRB utilization percentage
- Available PRBs per cell

**Control Actions Enabled:**
- PRB allocation (QoS) - allocate based on available resources
- Cell on/off - turn off cells with low utilization
- Handover - offload from overloaded cells

**Current Status:**
- ✅ DL PRB usage available (`RRU.PrbUsedDl`, `RRU.PrbUsedDl.UEID`)
- ❌ UL PRB usage NOT available

---

### 4. **BLER (Block Error Rate)**
**Why Critical:**
- **Link quality**: High BLER = poor transmission conditions
- **Power control**: Increase power if BLER is high
- **MCS adaptation**: Indicates if MCS is too aggressive

**What to Monitor:**
- Per-UE DL/UL BLER (%)
- Cell-level average BLER
- BLER trends (increasing = degrading conditions)

**Control Actions Enabled:**
- TX power adjustment (increase power if BLER > threshold)
- Handover (move UEs with high BLER to better cells)
- PRB allocation (give more resources to UEs with high BLER)

**Current Status:**
- ❌ NOT available in KPM (but available in `RxPacketTrace.txt` trace files)

---

## Priority 2: Important KPIs (Should Have)

### 5. **Delay/Latency (PDCP Delay)**
**Why Important:**
- **QoS enforcement**: Some applications require low latency
- **Congestion detection**: High delay = congestion
- **Resource allocation**: Prioritize low-latency UEs

**What to Monitor:**
- Per-UE PDCP delay (DL/UL, milliseconds)
- Cell-level average delay
- Delay percentiles (p50, p95, p99)

**Control Actions Enabled:**
- PRB allocation (prioritize low-latency UEs)
- Handover (move UEs with high delay)

**Current Status:**
- ❌ NOT currently sent in KPM (but available in trace files)

---

### 6. **Active UE Count**
**Why Important:**
- **Load estimation**: Number of UEs per cell
- **Resource planning**: Estimate resource needs
- **Energy efficiency**: Turn off cells with few UEs

**What to Monitor:**
- Mean active UEs per cell (DL/UL)
- Current active UE count
- UE distribution across cells

**Control Actions Enabled:**
- Cell on/off decisions
- Load balancing (handover UEs from crowded cells)

**Current Status:**
- ✅ Available (`DRB.MeanActiveUeDl`)

---

### 7. **MCS (Modulation and Coding Scheme)**
**Why Important:**
- **Link quality indicator**: High MCS = good conditions, low MCS = poor conditions
- **Throughput correlation**: MCS directly affects achievable throughput
- **Power control**: Low MCS may indicate need for more power

**What to Monitor:**
- Per-UE DL/UL MCS distribution
- Cell-level MCS statistics
- MCS trends (decreasing = degrading conditions)

**Control Actions Enabled:**
- TX power adjustment
- Handover decisions
- PRB allocation (UEs with low MCS may need more PRBs)

**Current Status:**
- ❌ NOT available in KPM (but available in `RxPacketTrace.txt`, `DlMacStats.txt`)

---

## Priority 3: Nice to Have KPIs

### 8. **Buffer Status**
**Why Useful:**
- **Congestion indicator**: High buffer = data waiting to be transmitted
- **Resource allocation**: Prioritize UEs with large buffers
- **Traffic shaping**: Adjust CBR if buffers are full

**What to Monitor:**
- Per-UE buffer occupancy (bytes)
- Cell-level buffer statistics

**Current Status:**
- ❌ NOT available in KPM (but available in trace files as `DRB.BufferSize.Qos.UEID`)

---

### 9. **Transport Block Statistics**
**Why Useful:**
- **Modulation distribution**: Shows what modulation schemes are being used
- **Error patterns**: Can infer BLER from error counts

**What to Monitor:**
- TB counts by modulation (QPSK, 16QAM, 64QAM, 256QAM)
- Error TB counts

**Current Status:**
- ✅ Partially available (`TB.TotNbrDlInitial.Qpsk/16Qam/64Qam`)

---

## Recommended KPI Set for AI

### Minimum Viable Set (Priority 1):
1. ✅ **SINR** (serving + neighbors) - for handover and power control
2. ✅ **DL Throughput** (per UE) - for QoS and load balancing
3. ✅ **PRB Usage** (cell + per UE, DL) - for resource allocation
4. ❌ **BLER** (per UE, DL) - for link quality assessment

### Optimal Set (Priority 1 + 2):
Add:
5. ❌ **RSRP/RSRQ** (serving + neighbors) - better handover decisions
6. ❌ **UL Throughput** (per UE) - complete traffic picture
7. ❌ **Delay** (per UE) - QoS enforcement
8. ❌ **MCS** (per UE) - link quality indicator
9. ✅ **Active UE Count** - load estimation

### Complete Set (All Priorities):
Add:
10. ❌ **Buffer Status** - congestion detection
11. ✅ **TB Statistics** - modulation distribution

---

## Control Action → KPI Mapping

### Handover Control
**Required KPIs:**
- Serving cell SINR/RSRP/RSRQ
- Neighbor cell SINR/RSRP/RSRQ
- Throughput (to avoid moving high-throughput UEs unnecessarily)
- PRB usage (to avoid moving to overloaded cells)

### PRB Allocation (QoS)
**Required KPIs:**
- Per-UE throughput (identify UEs needing more resources)
- PRB usage (know available resources)
- Delay (prioritize low-latency UEs)
- Buffer status (prioritize UEs with data waiting)

### TX Power Control
**Required KPIs:**
- SINR/RSRP (signal quality)
- BLER (transmission quality)
- MCS (link quality)
- Throughput (ensure power increase helps)

### Cell On/Off (Energy Efficiency)
**Required KPIs:**
- Active UE count (few UEs = candidate for shutdown)
- PRB usage (low usage = candidate for shutdown)
- Neighbor cell SINR (ensure coverage maintained)
- Throughput (ensure no active traffic)

### Traffic Shaping (CBR)
**Required KPIs:**
- Throughput (adjust if too high/low)
- Delay (adjust if delay is high)
- Buffer status (adjust if buffers are full)
- PRB usage (adjust if resources are constrained)

---

## Recommendations

### Short Term (Use Current KPM Data):
Focus on:
1. **SINR** (available) - for handover decisions
2. **DL Throughput** (available) - for QoS decisions
3. **PRB Usage** (available) - for resource allocation
4. **Active UE Count** (available) - for load balancing

**Limitations:**
- No UL metrics
- No BLER
- No RSRP/RSRQ (only SINR)
- No delay metrics

### Medium Term (Parse Trace Files):
Add post-processing to extract:
- BLER from `RxPacketTrace.txt`
- MCS from `RxPacketTrace.txt` or `DlMacStats.txt`
- RSRP from `LteDlRsrpSinrStats.txt`
- UL metrics from `UlMacStats.txt`, `UlPdcpStats.txt`

### Long Term (Modify NS3):
Modify NS3 to include in KPM messages:
- RSRP/RSRQ (from RRC measurements)
- UL throughput (from PDCP/RLC stats)
- BLER (from MAC/PHY stats)
- Delay (from PDCP stats)
- MCS (from MAC stats)
- Buffer status (from RLC stats)

---

## AI Decision Logic Examples

### Example 1: Handover Decision
```
IF (neighbor_cell_SINR > serving_cell_SINR + 3dB) 
   AND (neighbor_cell_PRB_usage < 80%)
   AND (serving_cell_throughput < threshold)
THEN trigger_handover(ue, neighbor_cell)
```

### Example 2: PRB Allocation
```
IF (ue_throughput < target_throughput)
   AND (cell_available_PRBs > 0)
   AND (ue_delay > threshold)
THEN increase_prb_allocation(ue, percentage)
```

### Example 3: Power Control
```
IF (ue_SINR < -10dB)
   AND (ue_BLER > 10%)
   AND (ue_MCS < 10)
THEN increase_enb_tx_power(cell, +3dB)
```

### Example 4: Cell On/Off
```
IF (cell_active_UEs < 2)
   AND (cell_PRB_usage < 20%)
   AND (neighbor_cells_can_cover)
THEN turn_off_cell(cell)
```

---

## Conclusion

**Most Critical KPIs for AI:**
1. **SINR/RSRP/RSRQ** - Signal quality (handover, power control)
2. **Throughput (DL/UL)** - Performance (QoS, load balancing)
3. **PRB Usage** - Resource utilization (allocation, energy efficiency)
4. **BLER** - Link quality (power control, handover)

**Current Gap:**
- Missing: RSRP/RSRQ, UL throughput, BLER, delay, MCS
- Available: SINR, DL throughput, DL PRB usage, active UE count

**Recommendation:**
Start with available KPIs (SINR, DL throughput, PRB usage) for basic optimization, then add missing KPIs (especially BLER and RSRP/RSRQ) for more sophisticated decisions.


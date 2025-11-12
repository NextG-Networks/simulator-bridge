# NS3 Trace Files Analysis

## Overview

NS3 generates detailed trace files with all the measurements the other group extracted, but **this data is NOT sent through the E2/KPM interface**. The other group must have parsed these trace files directly.

## Available Trace Files

### 1. RSRP/SINR Measurements
**File:** `LteDlRsrpSinrStats.txt`
**Format:**
```
% time	cellId	IMSI	RNTI	rsrp	sinr	ComponentCarrierId
0.000214285	1	0	0	6.12646e-10	1.29157e+06	0
```
**Contains:**
- ✅ RSRP (Reference Signal Received Power)
- ✅ SINR (Signal to Interference plus Noise Ratio)
- Time, Cell ID, IMSI, RNTI

### 2. PHY Packet Trace (BLER, MCS, TB Size)
**File:** `RxPacketTrace.txt`
**Format:**
```
DL/UL	time	frame	subF	slot	1stSym	symbol#	cellId	rnti	ccId	tbSize	mcs	rv	SINR(dB)	corrupt	TBler
DL	0.112821	11	2	3	1	3	3	1	0	24	0	0	3.60211	0	0
```
**Contains:**
- ✅ DL/UL direction
- ✅ MCS (Modulation and Coding Scheme)
- ✅ TB Size (Transport Block size)
- ✅ SINR (dB)
- ✅ **BLER** (TBler - Transport Block Error Rate)
- ✅ Corrupt flag
- Time, Cell ID, RNTI

### 3. MAC Statistics
**Files:** `DlMacStats.txt`, `UlMacStats.txt`
**Format (from NS3 docs):**
```
time	cellId	IMSI	frame	subframe	RNTI	MCS_TB1	size_TB1	MCS_TB2	size_TB2
```
**Contains:**
- ✅ DL/UL MCS
- ✅ TB sizes
- Time, Cell ID, IMSI, RNTI

### 4. RLC Statistics
**Files:** `DlRlcStats.txt`, `UlRlcStats.txt`
**Format (from NS3 docs):**
```
start_time	end_time	cellId	IMSI	RNTI	LCID	txPDUs	txBytes	rxPDUs	rxBytes	avgDelay	stdDelay	minDelay	maxDelay	avgSize	stdSize	minSize	maxSize
```
**Contains:**
- ✅ DL/UL throughput (bytes)
- ✅ DL/UL PDU counts
- ✅ Delay statistics
- Time intervals, Cell ID, IMSI, RNTI

### 5. PDCP Statistics
**Files:** `DlPdcpStats.txt`, `UlPdcpStats.txt`
**Format (from NS3 docs):**
```
start_time	end_time	cellId	IMSI	RNTI	LCID	txPDUs	txBytes	rxPDUs	rxBytes	avgDelay	stdDelay	minDelay	maxDelay	avgSize	stdSize	minSize	maxSize
```
**Contains:**
- ✅ DL/UL throughput (bytes)
- ✅ DL/UL PDU counts
- ✅ Delay statistics
- Time intervals, Cell ID, IMSI, RNTI

### 6. UL SINR Statistics
**File:** `LteUlSinrStats.txt`
**Contains:**
- ✅ UL SINR
- Time, Cell ID, IMSI

### 7. UL Interference Statistics
**File:** `LteUlInterferenceStats.txt`
**Contains:**
- ✅ UL interference per RB
- Time, Cell ID

## What's Missing from KPM Messages

The E2/KPM interface only sends a **subset** of available measurements:

**Currently sent via KPM:**
- `DRB.UEThpDl.UEID` - DL throughput (from PDCP)
- `RRU.PrbUsedDl` - PRB usage (DL only)
- `TB.TotNbrDlInitial.Qpsk/16Qam/64Qam` - TB counts by modulation
- `DRB.MeanActiveUeDl` - Mean active UEs
- `HO.SrcCellQual.RS-SINR.UEID` - Serving cell SINR (RRC measurements)
- `HO.TrgtCellQual.RS-SINR.UEID` - Neighbor cell SINR (RRC measurements)

**Available in trace files but NOT sent via KPM:**
- ❌ **RSRP** - Available in `LteDlRsrpSinrStats.txt`
- ❌ **RSRQ** - Not directly available (would need to calculate from RSRP and RSSI)
- ❌ **UL Throughput** - Available in `UlPdcpStats.txt`, `UlRlcStats.txt`
- ❌ **UL PRB Usage** - Not directly available in trace files
- ❌ **DL/UL BLER** - Available in `RxPacketTrace.txt` (TBler column)
- ❌ **DL/UL MCS** - Available in `RxPacketTrace.txt`, `DlMacStats.txt`, `UlMacStats.txt`
- ❌ **DL/UL Buffer** - Available in RLC/PDCP stats
- ❌ **CQI, PMI, RI** - Not directly available in trace files
- ❌ **UL RSSI, UL SINR** - Available in `LteUlSinrStats.txt`

## Solutions

### Option 1: Parse Trace Files (Post-Processing)
**Pros:**
- No NS3 code changes needed
- All data is available
- Can be done after simulation

**Cons:**
- Not real-time
- Need to parse multiple files
- Data may be delayed

**Implementation:**
Create a Python script to parse the trace files and generate CSV files matching the other group's format.

### Option 2: Modify NS3 to Send via KPM (Real-Time)
**Pros:**
- Real-time data
- Integrated with existing KPM flow
- Available to xApp immediately

**Cons:**
- Requires NS3 code changes
- Need to rebuild NS3
- More complex

**Implementation:**
Modify `BuildRicIndicationMessageCuUp` and `BuildRicIndicationMessageCuCp` in:
- `ns-3-mmwave-oran/src/lte/model/lte-enb-net-device.cc`
- `ns-3-mmwave-oran/src/mmwave/model/mmwave-enb-net-device.cc`

Add measurements by accessing:
- RSRP/SINR from `m_l3sinrMap` or trace files
- BLER from MAC/PHY statistics
- UL throughput from PDCP/RLC statistics
- MCS from MAC statistics

## Recommendation

For **real-time analysis**, modify NS3 to include these measurements in KPM messages.

For **post-processing analysis**, create a script to parse the trace files and generate CSV files.


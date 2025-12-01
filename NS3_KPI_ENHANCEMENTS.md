# NS3 KPI Enhancements - Implementation Summary

## Overview

This document summarizes the enhancements made to NS3 to include additional KPIs in the KPM messages for better AI-driven network optimization.

## Changes Made

### 1. RSRP Storage and Reporting ✅

**Files Modified:**
- `ns-3-mmwave-oran/src/lte/model/lte-enb-net-device.h`
- `ns-3-mmwave-oran/src/lte/model/lte-enb-net-device.cc`

**Changes:**
- Added `m_l3rsrpMap` to store RSRP measurements (similar to `m_l3sinrMap` for SINR)
- Added `RegisterNewRsrpReading()` method to store RSRP values
- Modified `ReportCurrentCellRsrpSinr()` to also store RSRP (previously only SINR was stored)
- Added RSRP to file logging in `BuildRicIndicationMessageCuCp()`

**Status:**
- ✅ RSRP is now stored when reported by UEs
- ✅ RSRP is included in file logging (CuCp CSV files)
- ⚠️ RSRP inclusion in actual KPM messages depends on RRC measurement encoding (may require additional changes to `LteIndicationMessageHelper`)

### 2. BLER Calculation ✅

**Files Modified:**
- `ns-3-mmwave-oran/src/lte/model/lte-enb-net-device.cc`

**Changes:**
- Added BLER calculation in `BuildRicIndicationMessageCuUp()`
- BLER = 1.0 - (rxPackets / txPackets) from RLC stats
- Added BLER to file logging (CuUp CSV files)

**Status:**
- ✅ BLER is calculated from RLC statistics
- ✅ BLER is included in file logging
- ⚠️ BLER inclusion in actual KPM messages requires ASN.1 schema modification (not yet implemented)

### 3. Delay (Already Available) ✅

**Status:**
- ✅ Delay is already calculated and included in KPM messages via `AddCuUpUePmItem()`
- ✅ Delay is available in the xApp as `DRB.PdcpSduDelayDl.UEID`

## Current Status

### Available in KPM Messages (via E2):
- ✅ SINR (serving and neighbor cells)
- ✅ DL Throughput
- ✅ DL PRB Usage
- ✅ Delay (PDCP SDU delay)
- ✅ Active UE Count
- ✅ TB counts by modulation

### Available in File Logging (but not yet in KPM):
- ✅ RSRP (added to CuCp CSV)
- ✅ BLER (added to CuUp CSV)

### Not Yet Available:
- ❌ RSRQ (requires RRC measurement encoding changes)
- ❌ UL Throughput (NS3 limitation)
- ❌ UL PRB Usage (NS3 limitation)
- ❌ MCS (requires MAC stats integration)

## Next Steps

### To Include RSRP/RSRQ in KPM Messages:

1. **Check RRC Measurement Encoding:**
   - Verify if `LteIndicationMessageHelper` includes RSRP/RSRQ in RRC measurements
   - If not, modify the helper to include RSRP/RSRQ from `m_l3rsrpMap` or RRC measurement reports

2. **Modify ASN.1 Schema (for BLER):**
   - Add BLER field to E2SM-KPM schema
   - Update `LteIndicationMessageHelper` to include BLER in measurements
   - Update xApp decoding to handle BLER

### To Include Additional KPIs:

1. **MCS:**
   - Access MAC statistics to get MCS values
   - Add to KPM message structure

2. **UL Metrics:**
   - Requires NS3 modifications to support UL measurements
   - May require changes to PDCP/RLC statistics collection

## Testing

### Verify RSRP Storage:
```bash
# Check NS3 logs for RSRP registration
grep "RSRP" ns3_output.log
```

### Verify BLER Calculation:
```bash
# Check NS3 logs for BLER values
grep "BLER" ns3_output.log
```

### Verify File Logging:
```bash
# Check CuCp CSV for RSRP column
tail -n 5 cu_cp_*.csv

# Check CuUp CSV for BLER column
tail -n 5 cu_up_*.csv
```

## Notes

- The xApp is already capable of decoding RSRP/RSRQ from RRC measurements (see `msgs_proc.cc` lines 392-398, 476-477)
- RSRP/RSRQ will be automatically included in the xApp's JSON output if NS3 sends them in the RRC measurements part of the KPM message
- BLER requires additional ASN.1 schema changes to be included in the actual KPM messages (currently only in file logging)

## Impact on AI Optimization

With these enhancements, the AI will have access to:
- **RSRP** (via file logging, and potentially via KPM if RRC measurements include it)
- **BLER** (via file logging, and potentially via KPM after schema changes)
- **Delay** (already available via KPM)

This enables better:
- Handover decisions (RSRP + SINR)
- Power control (RSRP + BLER)
- QoS optimization (BLER + Delay)
- Link quality assessment (RSRP + BLER + SINR)


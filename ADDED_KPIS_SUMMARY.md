# Added KPIs to mmWave Nodes - Summary

## Changes Made

I've updated `mmwave-enb-net-device.cc` to include additional KPIs in the KPM messages, matching what the LTE version already sends.

## New Measurements Added

### Per-UE Measurements (in `BuildRicIndicationMessageCuUp`)

1. **PDCP Bytes DL** (`DRB.PdcpSduVolumeDl.UEID`)
   - Total PDCP bytes transmitted to the UE in downlink
   - Already calculated as `txBytes` (in kbit)
   - **Status**: ✅ Added to `AddCuUpUePmItem` call

2. **PDCP PDU Count DL** (`DRB.PdcpPduNbrDl.UEID`)
   - Number of PDCP PDUs transmitted to the UE
   - Already calculated as `txDlPackets`
   - **Status**: ✅ Added to `AddCuUpUePmItem` call

3. **PDCP Throughput** (`DRB.UEThpDl.UEID`)
   - Downlink throughput in kbps
   - Already calculated as `pdcpThroughput = txBytes / m_e2Periodicity`
   - **Status**: ✅ Added to `AddCuUpUePmItem` call

4. **PDCP Delay** (`DRB.PdcpSduDelayDl.UEID`)
   - Average PDCP SDU delay in downlink (units: x 0.1 ms)
   - Already calculated as `pdcpLatency`
   - **Status**: ✅ Added to `AddCuUpUePmItem` call

5. **BLER (Block Error Rate)** (`DRB.BlerDl.UEID`)
   - Downlink block error rate (0.0 to 1.0, where 1.0 = 100% error rate)
   - **NEW**: Calculated from RLC statistics
   - Formula: `BLER = 1.0 - (rxPackets / txPackets)`
   - **Status**: ✅ Added calculation and to `AddCuUpUePmItem` call

### Cell-Level Measurements (in `BuildRicIndicationMessageCuUp`)

1. **Average Cell Delay** (`DRB.PdcpSduDelayDl`)
   - Average PDCP delay across all UEs in the cell
   - Calculated as: `cellAverageLatency = perUserAverageLatencySum / ueMap.size()`
   - **Status**: ✅ Added via `AddCuUpCellPmItem(cellAverageLatency)`

2. **Cell PDCP Bytes DL** (`DRB.PdcpSduVolumeDl`)
   - Total PDCP bytes transmitted in downlink for the entire cell
   - Already calculated as `cellDlTxVolume`
   - **Status**: ✅ Added via `FillCuUpValues(plmId, 0, cellDlTxVolume)`

## Code Changes

### File: `ns-3-mmwave-oran/src/mmwave/model/mmwave-enb-net-device.cc`

**Lines 818-827**: Added BLER calculation
```cpp
// Calculate BLER from RLC stats: BLER = 1.0 - (rxPackets / txPackets)
double dlBler = 0.0;
uint64_t rxE2DlPduRlc = m_e2RlcStatsCalculator->GetDlRxPackets(imsi, 3);
if (txPdcpPduNrRlc > 0)
{
    dlBler = 1.0 - (static_cast<double>(rxE2DlPduRlc) / static_cast<double>(txPdcpPduNrRlc));
    dlBler = std::max(0.0, std::min(1.0, dlBler)); // Clamp between 0 and 1
}
```

**Lines 856-861**: Updated `AddCuUpUePmItem` call to include all measurements
```cpp
indicationMessageHelper->AddCuUpUePmItem(ueImsiComplete,
                                         txBytes,           // PDCP bytes
                                         txDlPackets,       // PDCP PDU count
                                         pdcpThroughput,    // Throughput
                                         pdcpLatency,       // Delay
                                         dlBler);           // BLER
```

**Lines 872-892**: Added cell-level measurements
```cpp
// get average cell latency
double cellAverageLatency = 0;
if (!ueMap.empty())
{
    cellAverageLatency = perUserAverageLatencySum / ueMap.size();
}

if (!indicationMessageHelper->IsOffline())
{
    indicationMessageHelper->AddCuUpCellPmItem(cellAverageLatency);
    indicationMessageHelper->FillCuUpValues(plmId, 0, cellDlTxVolume);
}
```

## Expected Measurement Names in KPM

Based on the LTE implementation, these measurements should be encoded with the following names:

### Per-UE:
- `DRB.PdcpSduVolumeDl.UEID` - PDCP bytes (txBytes)
- `DRB.PdcpPduNbrDl.UEID` - PDCP PDU count (txDlPackets)
- `DRB.UEThpDl.UEID` - Throughput (pdcpThroughput)
- `DRB.PdcpSduDelayDl.UEID` - Delay (pdcpLatency)
- `DRB.BlerDl.UEID` - BLER (dlBler)

### Cell-Level:
- `DRB.PdcpSduDelayDl` - Average cell delay
- `DRB.PdcpSduVolumeDl` - Total cell PDCP bytes

## xApp Decoding

The xApp's `extract_measurement` function in `msgs_proc.cc` automatically extracts any measurement by name or ID, so these should be decoded automatically.

## AI Dummy Server

The AI dummy server (`ai_dummy_server.py`) already has mappings for these measurements:
- ✅ `DRB.PdcpSduVolumeDl.UEID` → `UE_PDCP_Bytes_DL`
- ✅ `DRB.PdcpPduNbrDl.UEID` → `UE_PDCP_PDU_Count_DL`
- ✅ `DRB.PdcpSduDelayDl.UEID` → `UE_PDCP_Delay_DL_ms`
- ✅ `DRB.BlerDl.UEID` → `UE_BLER_DL_percent`

## CSV Output

After rebuilding NS3 and running the simulation, you should see these new columns in the CSV files:

### `ue_kpis.csv`:
- `UE_PDCP_Bytes_DL` - PDCP bytes transmitted to UE
- `UE_PDCP_PDU_Count_DL` - Number of PDCP PDUs
- `UE_PDCP_Delay_DL_ms` - Average PDCP delay (milliseconds)
- `UE_BLER_DL_percent` - Block error rate (0-100%)

### `gnb_kpis.csv`:
- `PDCP_Bytes_DL` - Total cell PDCP bytes
- `PDCP_SDU_Delay_DL_ms` - Average cell delay

## Next Steps

1. **Rebuild NS3:**
   ```bash
   cd ns-3-mmwave-oran
   ./ns3 build
   ```

2. **Run simulation** and check:
   - xApp logs for decoded measurements
   - AI dummy server output for new KPI fields
   - CSV files for new columns

3. **Verify measurements appear** in:
   - xApp JSON output (check logs)
   - AI dummy server console output
   - CSV files (`ue_kpis.csv` and `gnb_kpis.csv`)

## Notes

- **BLER units**: The value is between 0.0 and 1.0 (0% to 100%). The AI dummy server should convert to percentage (multiply by 100).
- **Delay units**: PDCP delay is in units of 0.1 ms. The AI dummy server should convert to milliseconds (divide by 10).
- **Throughput units**: Already in kbps, which can be converted to Mbps by dividing by 1000.

## Current vs. New Measurements

### Before (Current):
- Cell-level: TB counts, PRB usage, Mean active UEs
- Per-UE: Throughput only

### After (With Changes):
- Cell-level: TB counts, PRB usage, Mean active UEs, **PDCP bytes**, **Average delay**
- Per-UE: Throughput, **PDCP bytes**, **PDCP PDU count**, **Delay**, **BLER**


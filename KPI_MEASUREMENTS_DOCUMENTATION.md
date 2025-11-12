# KPI Measurements Documentation

This document explains what each measurement in the CSV files means.

## ⚠️ Important Note: What NS3 Actually Sends

Based on the NS3 source code analysis, **NS3 only sends the following measurements**:

### Currently Available Measurements:

**Cell-Level (gNB):**
- `TB.TotNbrDlInitial.Qpsk`, `TB.TotNbrDlInitial.16Qam`, `TB.TotNbrDlInitial.64Qam` - Transport block counts by modulation
- `RRU.PrbUsedDl` - PRB usage (downlink only)
- `DRB.MeanActiveUeDl` - Mean active UEs (downlink only)

**Per-UE:**
- `DRB.UEThpDl.UEID` - Downlink throughput (Mbps)
- `RRU.PrbUsedDl.UEID` - PRB used (downlink only) - **Available but may not be in all reports**
- `HO.SrcCellQual.RS-SINR.UEID` - Serving cell SINR (from RRC measurements)
- `HO.TrgtCellQual.RS-SINR.UEID` - Neighbor cell SINR (from RRC measurements)

### ❌ NOT Available (NS3 doesn't send these):

- **UL Throughput** - NS3 comment: `pDCPBytesUL = 0 since it is not supported from the simulator`
- **UL PRB Usage** - Not implemented
- **UL BLER** - Not implemented  
- **DL BLER** - Can be calculated from `TB.ErrTotalNbrDl.1` if available, but may not be in all reports
- **RSRP/RSRQ** - Only SINR is sent in RRC measurements, not RSRP/RSRQ

**To get missing measurements, you would need to modify NS3's `BuildRicIndicationMessageCuUp` and related functions in `lte-enb-net-device.cc` and `mmwave-enb-net-device.cc`.**

---

## gNB (Cell-Level) Measurements (`gnb_kpis.csv`)

### Basic Fields
- `timestamp`: ISO 8601 timestamp when the measurement was taken
- `meid`: Management Entity ID (e.g., "gnb:131-133-32000000")
- `cell_id`: Cell identifier (e.g., "1112", "1113")
- `format`: KPM format type (usually "F1")

### Throughput & Data Volume
- `PDCP_Bytes_DL`: Total PDCP bytes transmitted in downlink (bytes) - **Not currently sent by NS3**
- `PDCP_Bytes_UL`: Total PDCP bytes received in uplink (bytes) - **❌ NOT AVAILABLE** (NS3 doesn't support UL)
- `PDCP_PDU_Count_DL`: Number of PDCP PDUs transmitted in downlink - **Not currently sent by NS3**
- `PDCP_PDU_Count_UL`: Number of PDCP PDUs received in uplink - **❌ NOT AVAILABLE**

### Resource Usage
- `PRB_Used_DL`: Physical Resource Blocks used in downlink ✅ **Available**
- `PRB_Used_UL`: Physical Resource Blocks used in uplink - **❌ NOT AVAILABLE**
- `PRB_Available_DL`: Physical Resource Blocks available in downlink - **Not currently sent by NS3**
- `PRB_Available_UL`: Physical Resource Blocks available in uplink - **❌ NOT AVAILABLE**

### Transport Block Statistics
- `DL_TB_QPSK_Count`: Number of downlink transport blocks using QPSK modulation
- `DL_TB_16QAM_Count`: Number of downlink transport blocks using 16QAM modulation
- `DL_TB_64QAM_Count`: Number of downlink transport blocks using 64QAM modulation
- `DL_TB_256QAM_Count`: Number of downlink transport blocks using 256QAM modulation

### Quality Metrics
- `PDCP_SDU_Delay_DL_ms`: Average PDCP SDU delay in downlink (milliseconds) - **Not currently sent by NS3**
- `PDCP_SDU_Delay_UL_ms`: Average PDCP SDU delay in uplink (milliseconds) - **❌ NOT AVAILABLE**
- `BLER_DL_percent`: Block Error Rate in downlink (percentage, 0-100) - **Not directly available** (could calculate from error counts if present)
- `BLER_UL_percent`: Block Error Rate in uplink (percentage, 0-100) - **❌ NOT AVAILABLE**

### UE Statistics
- `Mean_Active_UEs_DL`: Average number of active UEs in downlink
- `Mean_Active_UEs_UL`: Average number of active UEs in uplink

---

## UE (Per-User) Measurements (`ue_kpis.csv`)

### Basic Fields
- `timestamp`: ISO 8601 timestamp when the measurement was taken
- `meid`: Management Entity ID (e.g., "gnb:131-133-32000000")
- `cell_id`: Cell identifier (e.g., "1112", "1113")
- `ue_id`: UE identifier (hexadecimal format, e.g., "3030303039")

### Throughput
- `UE_Throughput_DL_Mbps`: Downlink throughput for this UE (Megabits per second) ✅ **Available** (as `DRB.UEThpDl.UEID`)
- `UE_Throughput_UL_Mbps`: Uplink throughput for this UE (Megabits per second) - **❌ NOT AVAILABLE** (NS3 doesn't support UL)

### Resource Usage
- `UE_PRB_Used_DL`: Physical Resource Blocks allocated to this UE in downlink ✅ **Available** (as `RRU.PrbUsedDl.UEID`, but may not be in all reports)
- `UE_PRB_Used_UL`: Physical Resource Blocks allocated to this UE in uplink - **❌ NOT AVAILABLE**

### Data Volume
- `UE_PDCP_Bytes_DL`: PDCP bytes transmitted to this UE in downlink (bytes) - **Not currently sent by NS3**
- `UE_PDCP_Bytes_UL`: PDCP bytes received from this UE in uplink (bytes) - **❌ NOT AVAILABLE**
- `UE_PDCP_PDU_Count_DL`: Number of PDCP PDUs transmitted to this UE in downlink - **Not currently sent by NS3**
- `UE_PDCP_PDU_Count_UL`: Number of PDCP PDUs received from this UE in uplink - **❌ NOT AVAILABLE**

### Quality Metrics
- `UE_PDCP_Delay_DL_ms`: Average PDCP SDU delay for this UE in downlink (milliseconds) - **Not currently sent by NS3**
- `UE_PDCP_Delay_UL_ms`: Average PDCP SDU delay for this UE in uplink (milliseconds) - **❌ NOT AVAILABLE**
- `UE_BLER_DL_percent`: Block Error Rate for this UE in downlink (percentage, 0-100) - **Not directly available** (could calculate from error counts if present)
- `UE_BLER_UL_percent`: Block Error Rate for this UE in uplink (percentage, 0-100) - **❌ NOT AVAILABLE**

### Signal Quality (RSRP/RSRQ/SINR)
- `RSRP_dBm`: Reference Signal Received Power (dBm) - signal strength from serving cell - **❌ NOT AVAILABLE** (NS3 only sends SINR, not RSRP)
- `RSRQ_dB`: Reference Signal Received Quality (dB) - signal quality from serving cell - **❌ NOT AVAILABLE** (NS3 only sends SINR, not RSRQ)
- `SINR_dB`: Signal to Interference plus Noise Ratio (dB) - interference indicator ✅ **Available** (from `HO.SrcCellQual.RS-SINR.UEID` and `HO.TrgtCellQual.RS-SINR.UEID`)
- `Target_RSRP_dBm`: RSRP from target cell (for handover decisions) - **❌ NOT AVAILABLE**
- `Target_RSRQ_dB`: RSRQ from target cell (for handover decisions) - **❌ NOT AVAILABLE**
- `Target_SINR_dB`: SINR from target cell (for handover decisions) ✅ **Available** (from `HO.TrgtCellQual.RS-SINR.UEID`)
- `ServingCell0_RSRP_dBm`: RSRP from serving cell (if multiple serving cells) - **❌ NOT AVAILABLE**
- `NeighborCell0_RSRP_dBm`: RSRP from first neighbor cell - **❌ NOT AVAILABLE**
- `NeighborCell1_RSRP_dBm`: RSRP from second neighbor cell - **❌ NOT AVAILABLE**
- `NeighborCell2_RSRP_dBm`: RSRP from third neighbor cell - **❌ NOT AVAILABLE**

### RRC Events
- `*_rrcEvent`: RRC measurement event type (b1, a3, a5, periodic) - indicates handover trigger type

---

## Measurement Units

- **Throughput**: Megabits per second (Mbps)
- **Bytes**: Bytes (B)
- **PRB**: Physical Resource Blocks (count)
- **Delay**: Milliseconds (ms)
- **BLER**: Percentage (0-100)
- **RSRP**: Decibel-milliwatts (dBm) - typically ranges from -140 to -40 dBm
- **RSRQ**: Decibels (dB) - typically ranges from -20 to -3 dB
- **SINR**: Decibels (dB) - typically ranges from -10 to 30 dB

---

## Notes

1. **Missing Measurements**: If a measurement is not present in a row, the cell will be empty. This is normal - not all measurements are reported in every KPI update.

2. **Measurement Names**: The measurement names in the CSV are human-readable labels. The original KPM measurement names (e.g., "DRB.UEThpDl") are mapped to these labels for clarity.

3. **RRC Measurements**: RSRP, RSRQ, and SINR are extracted from RRC measurement reports. These may not be present in every KPI update - they're typically reported periodically or during handover events.

4. **BLER**: Block Error Rate indicates the percentage of transport blocks that failed. Lower is better (0% = perfect, 100% = all failed).

5. **Throughput Calculation**: Throughput is calculated from PDCP bytes over the measurement period. The actual value depends on the measurement window size.


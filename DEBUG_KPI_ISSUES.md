# Debug: Missing KPIs in CSV Files

## Issue
RSRP, BLER, and delay are not appearing in the CSV files even though:
- Delay is being sent in KPM messages as `DRB.PdcpSduDelayDl.UEID`
- BLER is now being added to KPM messages as `DRB.BlerDl.UEID`
- RSRP is stored but not yet in KPM messages

## Current Status

### Delay (`DRB.PdcpSduDelayDl.UEID`)
- ✅ **NS3 sends it**: Line 57 in `lte-indication-message-helper.cc` adds it to KPM
- ❓ **xApp decodes it**: Need to verify if xApp extracts it from per-UE measurements
- ❓ **CSV writes it**: Code is ready but measurement might not be in JSON

### BLER (`DRB.BlerDl.UEID`)
- ✅ **NS3 sends it**: Just added to `AddCuUpUePmItem` (line 61)
- ❓ **xApp decodes it**: Should be extracted automatically by xApp
- ✅ **CSV writes it**: Code is ready with label `UE_BLER_DL_percent`

### RSRP
- ✅ **NS3 stores it**: In `m_l3rsrpMap`
- ❌ **NS3 sends it**: NOT yet in KPM messages (only in file logging)
- ✅ **xApp can decode it**: From RRC measurements if present
- ✅ **CSV writes it**: Code is ready to extract from `servingCell.rsrp`

## Next Steps

1. **Rebuild NS3** with the BLER changes
2. **Check xApp logs** to see what measurements are actually being decoded
3. **Verify JSON output** from xApp to see if delay/BLER are present
4. **Add RSRP to KPM** if needed (may require RRC measurement encoding)

## Testing

To verify what's being sent:
1. Check NS3 logs for BLER calculation
2. Check xApp logs for decoded measurements
3. Check AI dummy server output for received JSON
4. Verify CSV files have new columns


#!/usr/bin/env python3
import socket
import struct
import json
import hashlib
import csv
import os
from datetime import datetime
from collections import defaultdict

HOST = "0.0.0.0"
PORT = 5000
MAX_FRAME = 1024 * 1024  # 1MB

# CSV output files
CSV_GNB_FILE = "gnb_kpis.csv"
CSV_UE_FILE = "ue_kpis.csv"

# Store previous values for change detection
prev_values = defaultdict(dict)

# Store last message for deduplication (per connection)
# Use a dict keyed by connection address to avoid cross-connection interference
last_message_hashes = {}  # {addr: (hash, time)}
DEDUP_WINDOW_MS = 100  # Skip duplicate messages within 100ms

# Verbose mode - set to True to see all messages including filtered ones
VERBOSE_MODE = True

# CSV writers and file handles
gnb_csv_file = None
ue_csv_file = None
gnb_csv_writer = None
ue_csv_writer = None
gnb_fieldnames = None
ue_fieldnames = None

# Measurement name to human-readable label mapping
MEASUREMENT_LABELS = {
    # Cell-level (gNB) measurements
    "TB.TotNbrDlInitial.Qpsk": "DL_TB_QPSK_Count",
    "TB.TotNbrDlInitial.16Qam": "DL_TB_16QAM_Count",
    "TB.TotNbrDlInitial.64Qam": "DL_TB_64QAM_Count",
    "TB.TotNbrDlInitial.256Qam": "DL_TB_256QAM_Count",
    "RRU.PrbUsedDl": "PRB_Used_DL",
    "RRU.PrbUsedDl.UEID": "UE_PRB_Used_DL",  # Per-UE PRB usage
    "RRU.PrbUsedUl": "PRB_Used_UL",
    "RRU.PrbUsedUl.UEID": "UE_PRB_Used_UL",
    "RRU.PrbAvailDl": "PRB_Available_DL",
    "RRU.PrbAvailUl": "PRB_Available_UL",
    "DRB.MeanActiveUeDl": "Mean_Active_UEs_DL",
    "DRB.MeanActiveUeUl": "Mean_Active_UEs_UL",
    "DRB.PdcpSduDelayDl": "PDCP_SDU_Delay_DL_ms",
    "DRB.PdcpSduDelayUl": "PDCP_SDU_Delay_UL_ms",
    "DRB.PdcpSduVolumeDl": "PDCP_Bytes_DL",
    "DRB.PdcpSduVolumeUl": "PDCP_Bytes_UL",
    "DRB.PdcpPduNbrDl": "PDCP_PDU_Count_DL",
    "DRB.PdcpPduNbrUl": "PDCP_PDU_Count_UL",
    
    # Per-UE measurements
    "DRB.UEThpDl": "UE_Throughput_DL_Mbps",
    "DRB.UEThpDl.UEID": "UE_Throughput_DL_Mbps",  # This is what NS3 actually sends
    "DRB.UEThpUl": "UE_Throughput_UL_Mbps",
    "DRB.UEThpUl.UEID": "UE_Throughput_UL_Mbps",
    "DRB.PdcpSduDelayDl.UEID": "UE_PDCP_Delay_DL_ms",
    "DRB.PdcpSduDelayUl.UEID": "UE_PDCP_Delay_UL_ms",
    "DRB.PdcpSduDelayDl": "UE_PDCP_Delay_DL_ms",  # Alternative format
    "DRB.PdcpSduDelayUl": "UE_PDCP_Delay_UL_ms",  # Alternative format
    "DRB.PdcpSduVolumeDl.UEID": "UE_PDCP_Bytes_DL",
    "DRB.PdcpSduVolumeUl.UEID": "UE_PDCP_Bytes_UL",
    "DRB.PdcpPduNbrDl.UEID": "UE_PDCP_PDU_Count_DL",
    "DRB.PdcpPduNbrUl.UEID": "UE_PDCP_PDU_Count_UL",
    "RRU.PrbUsedDl.UEID": "UE_PRB_Used_DL",
    "RRU.PrbUsedUl.UEID": "UE_PRB_Used_UL",
    
    # RRC measurements (signal quality) - Note: These are SINR, not RSRP!
    "HO.SrcCellQual.RS-SINR.UEID": "SINR_dB",  # Serving cell SINR
    "HO.TrgtCellQual.RS-SINR.UEID": "Target_SINR_dB",  # Target cell SINR
    "HO.SrcCellQual.RS-RSRQ.UEID": "RSRQ_dB",
    "HO.TrgtCellQual.RS-RSRQ.UEID": "Target_RSRQ_dB",
    "HO.SrcCellQual.RS-SINR": "SINR_dB",
    "HO.TrgtCellQual.RS-SINR": "Target_SINR_dB",
    
    # BLER (Block Error Rate) - if present
    "DRB.BlerDl": "BLER_DL_percent",
    "DRB.BlerUl": "BLER_UL_percent",
    "DRB.BlerDl.UEID": "UE_BLER_DL_percent",
    "DRB.BlerUl.UEID": "UE_BLER_UL_percent",
}

def get_measurement_label(name):
    """Convert measurement name to human-readable label"""
    # Check exact match first
    if name in MEASUREMENT_LABELS:
        return MEASUREMENT_LABELS[name]
    
    # Check partial matches for common patterns
    if "UEThpDl" in name or "ThpDl" in name or "ThroughputDl" in name:
        return "UE_Throughput_DL_Mbps" if "UEID" in name or "UE" in name else "Throughput_DL_Mbps"
    if "UEThpUl" in name or "ThpUl" in name or "ThroughputUl" in name:
        return "UE_Throughput_UL_Mbps" if "UEID" in name or "UE" in name else "Throughput_UL_Mbps"
    if "Delay" in name or "delay" in name:
        if "UEID" in name or "UE" in name:
            return "UE_PDCP_Delay_DL_ms" if "Dl" in name or "DL" in name else "UE_PDCP_Delay_UL_ms"
        return "PDCP_SDU_Delay_DL_ms" if "Dl" in name or "DL" in name else "PDCP_SDU_Delay_UL_ms"
    if "BlerDl" in name or "BLERDl" in name:
        return "BLER_DL_percent"
    if "BlerUl" in name or "BLERUl" in name:
        return "BLER_UL_percent"
    if "PrbUsedDl" in name or "PRBUsedDl" in name:
        return "PRB_Used_DL"
    if "PrbUsedUl" in name or "PRBUsedUl" in name:
        return "PRB_Used_UL"
    if "RSRP" in name or "rsrp" in name:
        return "RSRP_dBm"
    if "RSRQ" in name or "rsrq" in name:
        return "RSRQ_dB"
    if "SINR" in name or "sinr" in name or "RSSINR" in name:
        return "SINR_dB"
    
    # Default: sanitize the name
    return name.replace(".", "_").replace(" ", "_")

def init_csv_files():
    """Initialize CSV files with headers"""
    global gnb_csv_file, ue_csv_file, gnb_csv_writer, ue_csv_writer, gnb_fieldnames, ue_fieldnames
    
    # Initialize gNB CSV
    # Always create new file to ensure proper headers (or read existing to get all columns)
    file_exists = os.path.exists(CSV_GNB_FILE)
    if file_exists:
        # Read existing file to get all columns
        with open(CSV_GNB_FILE, 'r') as f:
            reader = csv.DictReader(f)
            gnb_fieldnames = reader.fieldnames or ['timestamp', 'meid', 'cell_id', 'format']
    else:
        gnb_fieldnames = ['timestamp', 'meid', 'cell_id', 'format']
    
    gnb_csv_file = open(CSV_GNB_FILE, 'a', newline='')
    gnb_csv_writer = csv.DictWriter(gnb_csv_file, fieldnames=gnb_fieldnames, extrasaction='ignore')
    if not file_exists:
        gnb_csv_writer.writeheader()
    
    # Initialize UE CSV
    file_exists = os.path.exists(CSV_UE_FILE)
    if file_exists:
        # Read existing file to get all columns
        with open(CSV_UE_FILE, 'r') as f:
            reader = csv.DictReader(f)
            ue_fieldnames = reader.fieldnames or ['timestamp', 'meid', 'cell_id', 'ue_id']
    else:
        ue_fieldnames = ['timestamp', 'meid', 'cell_id', 'ue_id']
    
    ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
    ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
    if not file_exists:
        ue_csv_writer.writeheader()
    
    print(f"[AI-DUMMY] CSV logging enabled: {CSV_GNB_FILE}, {CSV_UE_FILE}")

def close_csv_files():
    """Close CSV files"""
    global gnb_csv_file, ue_csv_file
    if gnb_csv_file:
        gnb_csv_file.close()
    if ue_csv_file:
        ue_csv_file.close()

def write_gnb_csv(timestamp, meid, cell_id, format_type, measurements):
    """Write gNB (cell-level) measurements to CSV"""
    global gnb_csv_writer, gnb_fieldnames, gnb_csv_file
    
    if not gnb_csv_writer:
        return
    
    # Build row with base fields
    row = {
        'timestamp': timestamp,
        'meid': meid,
        'cell_id': cell_id,
        'format': format_type
    }
    
    # Add all measurements as columns
    for meas in measurements:
        name = meas.get("name", f"id_{meas.get('id', 'unknown')}")
        value = meas.get("value", "")
        # Convert to human-readable label
        csv_name = get_measurement_label(name)
        row[csv_name] = value
        
        # Add to fieldnames if new (need to rewrite file with new header)
        if csv_name not in gnb_fieldnames:
            gnb_fieldnames.append(csv_name)
            # Close and reopen to update fieldnames
            gnb_csv_file.close()
            # Read existing data
            existing_data = []
            if os.path.exists(CSV_GNB_FILE):
                with open(CSV_GNB_FILE, 'r') as f:
                    reader = csv.DictReader(f)
                    existing_data = list(reader)
            # Rewrite file with new header
            gnb_csv_file = open(CSV_GNB_FILE, 'w', newline='')
            gnb_csv_writer = csv.DictWriter(gnb_csv_file, fieldnames=gnb_fieldnames, extrasaction='ignore')
            gnb_csv_writer.writeheader()
            # Write existing data back
            for old_row in existing_data:
                gnb_csv_writer.writerow(old_row)
            # Reopen in append mode
            gnb_csv_file.close()
            gnb_csv_file = open(CSV_GNB_FILE, 'a', newline='')
            gnb_csv_writer = csv.DictWriter(gnb_csv_file, fieldnames=gnb_fieldnames, extrasaction='ignore')
    
    gnb_csv_writer.writerow(row)
    gnb_csv_file.flush()  # Ensure data is written immediately

def write_ue_csv(timestamp, meid, cell_id, ue_id, measurements):
    """Write UE measurements to CSV"""
    global ue_csv_writer, ue_fieldnames, ue_csv_file
    
    if not ue_csv_writer:
        return
    
    # Build row with base fields
    row = {
        'timestamp': timestamp,
        'meid': meid,
        'cell_id': cell_id,
        'ue_id': ue_id
    }
    
    # Add all measurements as columns
    for meas in measurements:
        name = meas.get("name", f"id_{meas.get('id', 'unknown')}")
        value = meas.get("value", "")
        
        # Handle RRC measurements (nested JSON) - flatten them
        if value == "" and "rrcEvent" in meas:
            # This is an RRC measurement, extract nested fields
            rrc_event = meas.get("rrcEvent", "")
            if rrc_event:
                csv_name = f"{name}_rrcEvent"
                row[csv_name] = rrc_event
                if csv_name not in ue_fieldnames:
                    ue_fieldnames.append(csv_name)
            
            # Extract serving cell RSRP/RSRQ/SINR
            serving_cells = meas.get("servingCells", [])
            if serving_cells:
                for idx, sc in enumerate(serving_cells):
                    signal_quality = sc.get("signalQuality", {})
                    if signal_quality:
                        for sq_key, sq_value in signal_quality.items():
                            # Map signal quality keys to readable names
                            sq_label = {"rsrp": "RSRP_dBm", "rsrq": "RSRQ_dB", "sinr": "SINR_dB"}.get(sq_key.lower(), sq_key)
                            csv_name = f"ServingCell{idx}_{sq_label}" if idx > 0 else sq_label
                            row[csv_name] = sq_value
                            if csv_name not in ue_fieldnames:
                                ue_fieldnames.append(csv_name)
                                # Need to rewrite file - same logic as above
                                ue_csv_file.close()
                                existing_data = []
                                if os.path.exists(CSV_UE_FILE):
                                    with open(CSV_UE_FILE, 'r') as f:
                                        reader = csv.DictReader(f)
                                        existing_data = list(reader)
                                ue_csv_file = open(CSV_UE_FILE, 'w', newline='')
                                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                                ue_csv_writer.writeheader()
                                for old_row in existing_data:
                                    ue_csv_writer.writerow(old_row)
                                ue_csv_file.close()
                                ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
                                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
            
            # Extract serving cell RSRP/RSRQ from EUTRA format (LTE)
            serving_cell = meas.get("servingCell", {})
            if serving_cell:
                if "rsrp" in serving_cell:
                    csv_name = "RSRP_dBm"
                    row[csv_name] = serving_cell["rsrp"]
                    if csv_name not in ue_fieldnames:
                        ue_fieldnames.append(csv_name)
                        # Need to rewrite file
                        ue_csv_file.close()
                        existing_data = []
                        if os.path.exists(CSV_UE_FILE):
                            with open(CSV_UE_FILE, 'r') as f:
                                reader = csv.DictReader(f)
                                existing_data = list(reader)
                        ue_csv_file = open(CSV_UE_FILE, 'w', newline='')
                        ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                        ue_csv_writer.writeheader()
                        for old_row in existing_data:
                            ue_csv_writer.writerow(old_row)
                        ue_csv_file.close()
                        ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
                        ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                if "rsrq" in serving_cell:
                    csv_name = "RSRQ_dB"
                    row[csv_name] = serving_cell["rsrq"]
                    if csv_name not in ue_fieldnames:
                        ue_fieldnames.append(csv_name)
                        # Need to rewrite file
                        ue_csv_file.close()
                        existing_data = []
                        if os.path.exists(CSV_UE_FILE):
                            with open(CSV_UE_FILE, 'r') as f:
                                reader = csv.DictReader(f)
                                existing_data = list(reader)
                        ue_csv_file = open(CSV_UE_FILE, 'w', newline='')
                        ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                        ue_csv_writer.writeheader()
                        for old_row in existing_data:
                            ue_csv_writer.writerow(old_row)
                        ue_csv_file.close()
                        ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
                        ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
            
            # Extract neighbor cell measurements
            neighbor_cells = meas.get("neighborCells", [])
            if neighbor_cells:
                for idx, nc in enumerate(neighbor_cells[:3]):  # Limit to first 3 neighbors
                    signal_quality = nc.get("signalQuality", {})
                    if signal_quality:
                        for sq_key, sq_value in signal_quality.items():
                            # Map signal quality keys to readable names
                            sq_label = {"rsrp": "RSRP_dBm", "rsrq": "RSRQ_dB", "sinr": "SINR_dB"}.get(sq_key.lower(), sq_key)
                            csv_name = f"NeighborCell{idx}_{sq_label}"
                            row[csv_name] = sq_value
                            if csv_name not in ue_fieldnames:
                                ue_fieldnames.append(csv_name)
                                # Need to rewrite file - same logic as above
                                ue_csv_file.close()
                                existing_data = []
                                if os.path.exists(CSV_UE_FILE):
                                    with open(CSV_UE_FILE, 'r') as f:
                                        reader = csv.DictReader(f)
                                        existing_data = list(reader)
                                ue_csv_file = open(CSV_UE_FILE, 'w', newline='')
                                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                                ue_csv_writer.writeheader()
                                for old_row in existing_data:
                                    ue_csv_writer.writerow(old_row)
                                ue_csv_file.close()
                                ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
                                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
        else:
            # Regular measurement (simple value)
            # Convert to human-readable label
            csv_name = get_measurement_label(name)
            row[csv_name] = value
            
            # Add to fieldnames if new (need to rewrite file with new header)
            if csv_name not in ue_fieldnames:
                ue_fieldnames.append(csv_name)
                # Close and reopen to update fieldnames
                ue_csv_file.close()
                # Read existing data
                existing_data = []
                if os.path.exists(CSV_UE_FILE):
                    with open(CSV_UE_FILE, 'r') as f:
                        reader = csv.DictReader(f)
                        existing_data = list(reader)
                # Rewrite file with new header
                ue_csv_file = open(CSV_UE_FILE, 'w', newline='')
                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
                ue_csv_writer.writeheader()
                # Write existing data back
                for old_row in existing_data:
                    ue_csv_writer.writerow(old_row)
                # Reopen in append mode
                ue_csv_file.close()
                ue_csv_file = open(CSV_UE_FILE, 'a', newline='')
                ue_csv_writer = csv.DictWriter(ue_csv_file, fieldnames=ue_fieldnames, extrasaction='ignore')
    
    ue_csv_writer.writerow(row)
    ue_csv_file.flush()  # Ensure data is written immediately

def recv_all(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data

def format_value(value):
    """Format numeric values with appropriate precision"""
    if isinstance(value, float):
        if value >= 1000:
            return f"{value:,.0f}"
        elif value >= 1:
            return f"{value:.2f}"
        else:
            return f"{value:.4f}"
    return str(value)

def print_kpi_table(msg, conn_addr=None):
    """Display KPI data in a readable table format
    Returns True if message was displayed, False if filtered out
    """
    global last_message_hashes
    
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    meid = msg.get("meid", "unknown")
    kpi = msg.get("kpi", {})
    
    if not kpi:
        if VERBOSE_MODE:
            print(f"[AI-DUMMY] Filtered: No KPI data in message")
        return False  # Skip empty messages
    
    # Deduplication: create hash of message content (per connection)
    msg_str = json.dumps(msg, sort_keys=True)
    msg_hash = hashlib.md5(msg_str.encode()).hexdigest()
    current_time = datetime.now()
    
    # Use connection address as key for deduplication
    conn_key = str(conn_addr) if conn_addr else "default"
    
    # Skip if this is the same message within the dedup window
    if conn_key in last_message_hashes:
        last_hash, last_time = last_message_hashes[conn_key]
        if (last_hash == msg_hash and 
            (current_time - last_time).total_seconds() * 1000 < DEDUP_WINDOW_MS):
            if VERBOSE_MODE:
                print(f"[AI-DUMMY] Filtered: Duplicate message (within {DEDUP_WINDOW_MS}ms)")
            return False  # Skip duplicate
    
    last_message_hashes[conn_key] = (msg_hash, current_time)
    
    # Extract cell ID - handle both string and numeric formats
    # Try to get from KPI first, then try to extract from MEID (e.g., "gnb:131-133-31000000" -> "1113")
    cell_id_raw = kpi.get("cellObjectID", None)
    
    # If cell ID is missing or invalid, try to extract from MEID
    if not cell_id_raw or cell_id_raw == "N/A" or cell_id_raw == "NRCellCU":
        # Try to extract cell ID from MEID format: "gnb:131-133-31000000" -> extract last part
        # MEID format: "gnb:131-133-XXXXXX" where XXXXXX might contain cell info
        meid_parts = meid.split(":")
        if len(meid_parts) >= 2:
            # Try to extract cell ID from the last part
            last_part = meid_parts[-1]
            # Sometimes it's like "31000000" where "1113" might be embedded
            # For now, just use the last part as fallback
            cell_id_raw = last_part[-4:] if len(last_part) >= 4 else last_part
        else:
            cell_id_raw = "unknown"
    
    cell_id = str(cell_id_raw)
    format_type = kpi.get("format", "N/A")
    
    # Get measurements and UEs
    measurements = kpi.get("measurements", [])
    ues = kpi.get("ues", [])
    
    # Filter out zero/empty measurements for cleaner display
    non_zero_measurements = [m for m in measurements if m.get("value", 0) != 0]
    ues_with_data = [ue for ue in ues if ue.get("measurements")]
    
    # Skip if no meaningful data
    if not non_zero_measurements and not ues_with_data:
        if VERBOSE_MODE:
            print(f"[AI-DUMMY] Filtered: All measurements are zero (measurements={len(measurements)}, ues={len(ues)})")
        return False  # Skip empty/zero-value messages
    
    print("\n" + "=" * 80)
    print(f"[{timestamp}] MEID: {meid} | Cell: {cell_id} | Format: {format_type}")
    print("=" * 80)
    
    # Cell-level measurements (only show non-zero)
    if non_zero_measurements:
        print("\n📊 Cell-Level Measurements:")
        print("-" * 80)
        print(f"{'Metric':<40} {'Value':<20} {'Change':<15}")
        print("-" * 80)
        
        for meas in non_zero_measurements:
            name = meas.get("name", "N/A")
            value = meas.get("value", 0)
            key = f"{meid}:{cell_id}:{name}"
            
            # Calculate change
            change_str = ""
            if key in prev_values:
                prev_val = prev_values[key]
                change = value - prev_val
                if abs(change) > 0.01:  # Only show significant changes
                    change_str = f"{change:+.2f}" if isinstance(change, float) else f"{change:+d}"
                    change_str = change_str[:14]  # Truncate if too long
            
            prev_values[key] = value
            
            # Shorten long metric names
            display_name = name[:37] + "..." if len(name) > 40 else name
            print(f"{display_name:<40} {format_value(value):<20} {change_str:<15}")
    
    # Per-UE measurements
    if ues_with_data:
        print(f"\n📱 Per-UE Measurements ({len(ues_with_data)} UEs):")
        print("-" * 80)
        
        # Collect all measurement types across UEs
        all_meas_types = set()
        for ue in ues_with_data:
            for meas in ue.get("measurements", []):
                if meas.get("value", 0) != 0:  # Only include non-zero measurements
                    all_meas_types.add(meas.get("name", "Unknown"))
        
        # Display in compact table format
        if all_meas_types:
            # Header
            header = f"{'UE ID':<15}"
            for meas_type in sorted(all_meas_types):
                # Shorten measurement name
                short_name = meas_type.replace("DRB.", "").replace("UEID", "").replace(".", "")
                short_name = short_name[:12] if len(short_name) <= 12 else short_name[:9] + "..."
                header += f" {short_name:<13}"
            print(header)
            print("-" * 80)
            
            # Data rows
            for ue in ues_with_data:
                ue_id = ue.get("ueId", "N/A")
                # Extract last few digits for readability
                if len(ue_id) > 10:
                    ue_id_display = "..." + ue_id[-10:]
                else:
                    ue_id_display = ue_id
                
                row = f"{ue_id_display:<15}"
                ue_measurements = {m.get("name"): m.get("value") for m in ue.get("measurements", []) if m.get("value", 0) != 0}
                
                for meas_type in sorted(all_meas_types):
                    value = ue_measurements.get(meas_type, "-")
                    if value != "-":
                        # Check for changes
                        key = f"{meid}:{cell_id}:{ue_id}:{meas_type}"
                        change_str = ""
                        if key in prev_values:
                            prev_val = prev_values[key]
                            change = value - prev_val
                            if abs(change) > 0.01:  # Only show significant changes
                                change_str = f" ({change:+.0f})" if isinstance(change, float) else f" ({change:+d})"
                                change_str = change_str[:12]
                        prev_values[key] = value
                        
                        val_str = format_value(value)
                        if len(val_str) + len(change_str) > 13:
                            val_str = val_str[:13-len(change_str)]
                        row += f" {val_str}{change_str:<{13-len(val_str)-len(change_str)}}"
                    else:
                        row += f" {'-':<13}"
                
                print(row)
    
    print("=" * 80 + "\n")
    
    # Write to CSV files
    iso_timestamp = datetime.now().isoformat()
    
    # Write gNB (cell-level) measurements
    if non_zero_measurements:
        write_gnb_csv(iso_timestamp, meid, cell_id, format_type, non_zero_measurements)
    
    # Write UE measurements (one row per UE)
    for ue in ues_with_data:
        ue_id = ue.get("ueId", "N/A")
        ue_measurements = [m for m in ue.get("measurements", []) if m.get("value", 0) != 0]
        if ue_measurements:
            write_ue_csv(iso_timestamp, meid, cell_id, ue_id, ue_measurements)
    
    return True  # Message was displayed

def handle_conn(conn, addr):
    print(f"[AI-DUMMY] Connected from {addr}")
    msg_count = 0
    try:
        while True:
            hdr = recv_all(conn, 4)
            if hdr is None:
                print(f"[AI-DUMMY] Client closed (received {msg_count} messages)")
                return
            (length,) = struct.unpack("!I", hdr)
            if length == 0 or length > MAX_FRAME:
                print(f"[AI-DUMMY] Invalid frame length={length}, closing")
                return
            body = recv_all(conn, length)
            if body is None:
                print("[AI-DUMMY] Incomplete frame body")
                return
            text = body.decode("utf-8", errors="replace")
            msg_count += 1
            
            try:
                msg = json.loads(text)
                msg_type = msg.get("type", "unknown")
                
                # Debug: always log that we received something
                print(f"[AI-DUMMY] Received message #{msg_count}: type={msg_type}, size={len(text)} bytes")
                
                if msg_type == "kpi":
                    # Display KPI in table format
                    printed = print_kpi_table(msg, addr)
                    if not printed and not VERBOSE_MODE:
                        # If print_kpi_table returned early (filtered), show a summary
                        kpi = msg.get("kpi", {})
                        cell_id = kpi.get("cellObjectID", "N/A")
                        measurements = kpi.get("measurements", [])
                        ues = kpi.get("ues", [])
                        print(f"[AI-DUMMY] Message filtered: cellId={cell_id}, measurements={len(measurements)}, ues={len(ues)}")
                elif msg_type == "recommendation_request":
                    # Also display KPI if it's a recommendation request
                    if "kpi" in msg:
                        printed = print_kpi_table(msg, addr)
                        if not printed and not VERBOSE_MODE:
                            kpi = msg.get("kpi", {})
                            cell_id = kpi.get("cellObjectID", "N/A")
                            print(f"[AI-DUMMY] Recommendation request filtered: cellId={cell_id}")
                    # Send reply
                    reply = json.dumps({"no_action": True}).encode("utf-8")
                    conn.sendall(struct.pack("!I", len(reply)) + reply)
                else:
                    # Fallback: print raw JSON for unknown types
                    print(f"[AI-DUMMY] Unknown message type: {msg_type}")
                    print(f"[AI-DUMMY] JSON: {text[:500]}...")
            except json.JSONDecodeError as e:
                # Non-JSON or malformed; print raw
                print(f"[AI-DUMMY] JSON decode error: {e}")
                print(f"[AI-DUMMY] Raw (first 500 chars): {text[:500]}...")
            except Exception as e:
                print(f"[AI-DUMMY] Error processing message: {e}")
                import traceback
                traceback.print_exc()
                print(f"[AI-DUMMY] Raw: {text[:500]}...")
    finally:
        conn.close()
        print(f"[AI-DUMMY] Connection closed (total messages: {msg_count})")

def main():
    # Initialize CSV files
    init_csv_files()
    
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((HOST, PORT))
            s.listen(5)
            print(f"[AI-DUMMY] Listening on {HOST}:{PORT}")
            while True:
                conn, addr = s.accept()
                handle_conn(conn, addr)
    except KeyboardInterrupt:
        print("\n[AI-DUMMY] Shutting down...")
    finally:
        close_csv_files()

if __name__ == "__main__":
    main()


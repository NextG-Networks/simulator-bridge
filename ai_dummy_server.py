#!/usr/bin/env python3
import socket
import struct
import json
import hashlib
from datetime import datetime
from collections import defaultdict

HOST = "0.0.0.0"
PORT = 5000
MAX_FRAME = 1024 * 1024  # 1MB

# Store previous values for change detection
prev_values = defaultdict(dict)

# Store last message for deduplication
last_message_hash = None
last_message_time = None
DEDUP_WINDOW_MS = 100  # Skip duplicate messages within 100ms

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

def print_kpi_table(msg):
    """Display KPI data in a readable table format"""
    global last_message_hash, last_message_time
    
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    meid = msg.get("meid", "unknown")
    kpi = msg.get("kpi", {})
    
    if not kpi:
        return  # Skip empty messages
    
    # Deduplication: create hash of message content
    msg_str = json.dumps(msg, sort_keys=True)
    msg_hash = hashlib.md5(msg_str.encode()).hexdigest()
    current_time = datetime.now()
    
    # Skip if this is the same message within the dedup window
    if (last_message_hash == msg_hash and 
        last_message_time and 
        (current_time - last_message_time).total_seconds() * 1000 < DEDUP_WINDOW_MS):
        return  # Skip duplicate
    
    last_message_hash = msg_hash
    last_message_time = current_time
    
    # Extract cell ID - handle both string and numeric formats
    cell_id_raw = kpi.get("cellObjectID", "N/A")
    if cell_id_raw == "N/A" or cell_id_raw == "NRCellCU" or not cell_id_raw:
        return  # Skip messages without valid cell ID
    
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
        return  # Skip empty/zero-value messages
    
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

def handle_conn(conn, addr):
    print(f"[AI-DUMMY] Connected from {addr}")
    try:
        while True:
            hdr = recv_all(conn, 4)
            if hdr is None:
                print("[AI-DUMMY] Client closed")
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
            
            try:
                msg = json.loads(text)
                msg_type = msg.get("type", "unknown")
                
                if msg_type == "kpi":
                    # Display KPI in table format
                    print_kpi_table(msg)
                elif msg_type == "recommendation_request":
                    # Also display KPI if it's a recommendation request
                    if "kpi" in msg:
                        print_kpi_table(msg)
                    # Send reply
                    reply = json.dumps({"no_action": True}).encode("utf-8")
                    conn.sendall(struct.pack("!I", len(reply)) + reply)
                else:
                    # Fallback: print raw JSON for unknown types
                    print(f"[AI-DUMMY] Unknown message type: {msg_type}")
                    print(f"[AI-DUMMY] JSON: {text}")
            except json.JSONDecodeError:
                # Non-JSON or malformed; print raw
                print(f"[AI-DUMMY] Raw (non-JSON): {text[:200]}...")
            except Exception as e:
                print(f"[AI-DUMMY] Error processing message: {e}")
                print(f"[AI-DUMMY] Raw: {text[:200]}...")
    finally:
        conn.close()

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(5)
        print(f"[AI-DUMMY] Listening on {HOST}:{PORT}")
        while True:
            conn, addr = s.accept()
            handle_conn(conn, addr)

if __name__ == "__main__":
    main()


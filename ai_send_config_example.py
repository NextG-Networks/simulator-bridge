#!/usr/bin/env python3
"""
Example: How AI sends configuration to xApp

The xApp receives configs on the same TCP connection as KPIs (port 5000 by default).
Configs are written to NS3 control files in real-time.
"""

import socket
import json
import struct
import os

def send_config(config_json, host="127.0.0.1", port=5001):
    """
    Send configuration JSON to xApp config receiver
    
    Args:
        config_json: JSON string or dict with config
        host: xApp host (default: 127.0.0.1)
        port: xApp config port (default: 5001, or AI_CONFIG_PORT env var)
    """
    if isinstance(config_json, dict):
        config_json = json.dumps(config_json)
    
    # Get port from env if set
    port = int(os.getenv("AI_CONFIG_PORT", port))
    
    # Create socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        sock.connect((host, port))
        
        # Send length-prefixed frame (same protocol as KPI messages)
        data = config_json.encode('utf-8')
        length = struct.pack('>I', len(data))  # 4-byte big-endian length
        sock.sendall(length + data)
        
        print(f"[AI] Sent config to {host}:{port}")
        print(f"[AI] Config: {config_json}")
        return True
        
    except Exception as e:
        print(f"[AI] Error sending config: {e}")
        return False
    finally:
        sock.close()


# Example 1: QoS/PRB allocation config
def example_qos_config():
    """
    Example: Allocate PRB percentages to UEs
    
    Note: AI sends IMSI values (e.g., "111000000000001").
    The xApp automatically converts IMSI to RNTI before writing to NS3 control files.
    Format: "111" (PLMN ID) + "000000000001" (UE number) -> RNTI = 1
    """
    config = {
        "type": "qos",
        "commands": [
            {
                "ueId": "111000000000001",  # IMSI format: PLMN(111) + UE_ID(000000000001) -> RNTI=1
                "percentage": 0.7  # 70% to LTE
            },
            {
                "ueId": "111000000000002",  # IMSI format: PLMN(111) + UE_ID(000000000002) -> RNTI=2
                "percentage": 0.5  # 50% to LTE
            }
        ]
    }
    send_config(config)


# Example 2: Handover config
def example_handover_config():
    """Example: Trigger handovers"""
    config = {
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
    send_config(config)


# Example 3: Energy efficiency config
def example_energy_config():
    """Example: Enable/disable cells"""
    config = {
        "type": "energy",
        "commands": [
            {
                "cellId": "1112",
                "hoAllowed": 0  # Disable handover to this cell
            },
            {
                "cellId": "1113",
                "hoAllowed": 1  # Enable handover to this cell
            }
        ]
    }
    send_config(config)


# Example 4: Dynamic config based on KPI analysis
def example_dynamic_config(kpi_data):
    """
    Example: Generate config based on received KPI data
    
    This would be called after receiving KPI from xApp
    """
    config = {
        "type": "qos",
        "commands": []
    }
    
    # Analyze KPI and generate commands
    kpi = kpi_data.get("kpi", {})
    ues = kpi.get("ues", [])
    
    for ue in ues:
        ue_id = ue.get("ueId")
        measurements = ue.get("measurements", [])
        
        # Find throughput measurement
        throughput = None
        for meas in measurements:
            if meas.get("name") == "DRB.UEThpDl.UEID":
                throughput = meas.get("value")
                break
        
        if throughput is not None:
            # Simple heuristic: low throughput = more resources
            if throughput < 3000:  # kbps
                # Convert hex UE ID to IMSI (example - adjust based on your format)
                try:
                    imsi = str(int(ue_id, 16)) if ue_id else None
                    if imsi:
                        config["commands"].append({
                            "ueId": imsi,
                            "percentage": 0.8  # Give 80% to LTE
                        })
                except ValueError:
                    pass
    
    if config["commands"]:
        send_config(config)
        return True
    return False


if __name__ == "__main__":
    print("AI Config Sender Examples")
    print("=" * 50)
    
    # Example: Send QoS config
    print("\n1. Sending QoS config...")
    example_qos_config()
    
    # Example: Send handover config
    print("\n2. Sending handover config...")
    example_handover_config()
    
    # Example: Send energy config
    print("\n3. Sending energy config...")
    example_energy_config()
    
    print("\nDone!")


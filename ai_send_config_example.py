#!/usr/bin/env python3
"""
Interactive AI Config Sender

Send configuration commands to xApp to control NS3 simulation.
Supports interactive mode or command-line arguments.
"""

import socket
import json
import struct
import os
import sys
import argparse

def send_config(config_json, host="127.0.0.1", port=5001):
    """
    Send configuration JSON to xApp config receiver
    
    Args:
        config_json: JSON string or dict with config
        host: xApp host (default: 127.0.0.1)
        port: xApp config port (default: 5001, or AI_CONFIG_PORT env var)
    """
    if isinstance(config_json, dict):
        config_json = json.dumps(config_json, indent=2)
    
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
        
        print(f"\n[AI] ✓ Sent config to {host}:{port}")
        print(f"[AI] Config:\n{config_json}\n")
        return True
        
    except Exception as e:
        print(f"[AI] ✗ Error sending config: {e}")
        return False
    finally:
        sock.close()


def get_input(prompt, default=None, input_type=str):
    """Get user input with optional default value"""
    if default is not None:
        prompt = f"{prompt} [{default}]: "
    else:
        prompt = f"{prompt}: "
    
    value = input(prompt).strip()
    if not value and default is not None:
        return default
    if not value:
        return None
    
    try:
        result = input_type(value)
        # Validate IMSI format if it's a string that looks like an IMSI
        if input_type == str and value.startswith("111") and len(value) < 15:
            print(f"Warning: IMSI should be 15 digits (e.g., 111000000000001), got {len(value)} digits")
            print(f"  Your input: {value}")
            print(f"  Expected format: 111000000000001 (PLMN 111 + 12-digit UE ID)")
            confirm = input("  Continue anyway? (y/n) [n]: ").strip().lower()
            if confirm != 'y':
                return None
        return result
    except ValueError:
        print(f"Invalid input, expected {input_type.__name__}")
        return None

def interactive_qos_config():
    """Interactive QoS/PRB allocation config"""
    print("\n=== QoS/PRB Allocation Configuration ===")
    print("Set PRB percentage allocation for UEs (0.0 to 1.0)")
    print("IMSI format: 111000000000001 (15 digits) -> RNTI 1")
    print("             111000000000002 (15 digits) -> RNTI 2, etc.\n")
    
    commands = []
    while True:
        ue_id = get_input("UE IMSI (e.g., 111000000000001)", None, str)
        if not ue_id:
            break
        
        percentage = get_input("PRB percentage (0.0-1.0)", None, float)
        if percentage is None or percentage < 0.0 or percentage > 1.0:
            print("Invalid percentage, must be between 0.0 and 1.0")
            continue
        
        commands.append({
            "ueId": ue_id,
            "percentage": percentage
        })
        
        more = get_input("Add another UE? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "qos", "commands": commands}
        return send_config(config)
    return False


def interactive_handover_config():
    """Interactive handover config"""
    print("\n=== Handover Configuration ===")
    print("Trigger handovers of UEs between cells")
    print("IMSI format: 111000000000001 (15 digits)")
    print("Cell ID format: 1112, 1113, etc.\n")
    
    commands = []
    while True:
        imsi = get_input("UE IMSI (e.g., 111000000000001)", None, str)
        if not imsi:
            break
        
        target_cell = get_input("Target Cell ID (e.g., 1112)", None, str)
        if not target_cell:
            break
        
        commands.append({
            "imsi": imsi,
            "targetCellId": target_cell
        })
        
        more = get_input("Add another handover? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "handover", "commands": commands}
        return send_config(config)
    return False


def interactive_energy_config():
    """Interactive energy efficiency config"""
    print("\n=== Energy Efficiency / Cell Control ===")
    print("Enable/disable handovers to specific cells")
    print("0 = disable (energy saving), 1 = enable\n")
    
    commands = []
    while True:
        cell_id = get_input("Cell ID (e.g., 1112)", None, str)
        if not cell_id:
            break
        
        ho_allowed = get_input("Handover allowed? (0=disable, 1=enable)", "1", int)
        if ho_allowed not in [0, 1]:
            print("Invalid value, must be 0 or 1")
            continue
        
        commands.append({
            "cellId": cell_id,
            "hoAllowed": ho_allowed
        })
        
        more = get_input("Add another cell? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "energy", "commands": commands}
        return send_config(config)
    return False

def interactive_enb_txpower_config():
    """Interactive eNB TX power config"""
    print("\n=== eNB Transmit Power Configuration ===")
    print("Adjust base station transmit power (typically 20-46 dBm)\n")
    
    commands = []
    while True:
        cell_id = get_input("Cell ID (e.g., 1112)", None, str)
        if not cell_id:
            break
        
        dbm = get_input("TX Power (dBm)", "43.0", float)
        if dbm is None:
            continue
        
        commands.append({
            "cellId": cell_id,
            "dbm": dbm
        })
        
        more = get_input("Add another eNB? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "set-enb-txpower", "commands": commands}
        return send_config(config)
    return False

def interactive_ue_txpower_config():
    """Interactive UE TX power config"""
    print("\n=== UE Transmit Power Configuration ===")
    print("Adjust UE transmit power (typically 10-23 dBm)")
    print("IMSI format: 111000000000001 (15 digits) -> RNTI 1, etc.\n")
    
    commands = []
    while True:
        ue_id = get_input("UE IMSI (e.g., 111000000000001)", None, str)
        if not ue_id:
            break
        
        dbm = get_input("TX Power (dBm)", "23.0", float)
        if dbm is None:
            continue
        
        commands.append({
            "ueId": ue_id,
            "dbm": dbm
        })
        
        more = get_input("Add another UE? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "set-ue-txpower", "commands": commands}
        return send_config(config)
    return False

def interactive_cbr_config():
    """Interactive CBR traffic rate config"""
    print("\n=== CBR Traffic Rate Configuration ===")
    print("Adjust traffic generation rate for all OnOffApplication instances")
    print("Rate format: '50Mbps', '1Gbps', '100kb/s', etc.\n")
    
    rate = get_input("Data Rate (e.g., 50Mbps)", None, str)
    pkt_bytes = get_input("Packet Size (bytes)", None, int)
    
    if not rate and not pkt_bytes:
        print("At least one parameter (rate or packet size) must be provided")
        return False
    
    config = {"type": "set-cbr"}
    if rate:
        config["rate"] = rate
    if pkt_bytes:
        config["pktBytes"] = pkt_bytes
    
    return send_config(config)

def interactive_prb_cap_config():
    """Interactive PRB cap config"""
    print("\n=== PRB Cap Configuration ===")
    print("Set maximum PRB allocation per UE (absolute limit)")
    print("IMSI format: 111000000000001 (15 digits) -> RNTI 1, etc.")
    print("Note: Requires scheduler modification for full support\n")
    
    commands = []
    while True:
        ue_id = get_input("UE IMSI (e.g., 111000000000001)", None, str)
        if not ue_id:
            break
        
        max_prb = get_input("Max PRBs (e.g., 10)", None, int)
        if max_prb is None or max_prb < 1:
            print("Invalid PRB value, must be >= 1")
            continue
        
        commands.append({
            "ueId": ue_id,
            "maxPrb": max_prb
        })
        
        more = get_input("Add another UE? (y/n)", "n", str)
        if more.lower() != 'y':
            break
    
    if commands:
        config = {"type": "cap-ue-prb", "commands": commands}
        return send_config(config)
    return False


def show_menu():
    """Display interactive menu"""
    print("\n" + "=" * 60)
    print("AI Config Sender - Interactive Mode")
    print("=" * 60)
    print("1. QoS/PRB Allocation (set PRB percentage per UE)")
    print("2. Handover Control (trigger UE handovers)")
    print("3. Energy Efficiency (enable/disable cells)")
    print("4. eNB TX Power (adjust base station power)")
    print("5. UE TX Power (adjust UE transmit power)")
    print("6. CBR Traffic Rate (adjust traffic generation)")
    print("7. PRB Cap (set max PRB limit per UE)")
    print("0. Exit")
    print("=" * 60)

def main():
    parser = argparse.ArgumentParser(
        description="Send configuration commands to xApp",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Interactive mode (default)
  python3 ai_send_config_example.py

  # Send QoS config directly
  python3 ai_send_config_example.py --qos --ue 111000000000001 --percentage 0.7

  # Send handover
  python3 ai_send_config_example.py --handover --imsi 111000000000001 --cell 1112

  # Send eNB TX power
  python3 ai_send_config_example.py --enb-txpower --cell 1112 --dbm 43.0
        """
    )
    
    parser.add_argument("--host", default="127.0.0.1", help="xApp host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=None, help="xApp port (default: 5001 or AI_CONFIG_PORT env)")
    
    # Command types
    parser.add_argument("--qos", action="store_true", help="Send QoS config")
    parser.add_argument("--handover", action="store_true", help="Send handover config")
    parser.add_argument("--energy", action="store_true", help="Send energy config")
    parser.add_argument("--enb-txpower", action="store_true", help="Send eNB TX power config")
    parser.add_argument("--ue-txpower", action="store_true", help="Send UE TX power config")
    parser.add_argument("--cbr", action="store_true", help="Send CBR config")
    parser.add_argument("--prb-cap", action="store_true", help="Send PRB cap config")
    
    # QoS args
    parser.add_argument("--ue", action="append", help="UE IMSI (can specify multiple)")
    parser.add_argument("--percentage", type=float, help="PRB percentage (0.0-1.0)")
    
    # Handover args
    parser.add_argument("--imsi", help="UE IMSI for handover")
    parser.add_argument("--cell", help="Target cell ID")
    
    # Energy args
    parser.add_argument("--ho-allowed", type=int, choices=[0, 1], help="Handover allowed (0 or 1)")
    
    # TX power args
    parser.add_argument("--dbm", type=float, help="TX power in dBm")
    
    # CBR args
    parser.add_argument("--rate", help="Data rate (e.g., 50Mbps)")
    parser.add_argument("--pkt-bytes", type=int, help="Packet size in bytes")
    
    # PRB cap args
    parser.add_argument("--max-prb", type=int, help="Maximum PRBs")
    
    args = parser.parse_args()
    
    # Set port from env or args
    port = args.port or int(os.getenv("AI_CONFIG_PORT", 5001))
    
    # If command-line args provided, use them
    if args.qos or args.handover or args.energy or args.enb_txpower or args.ue_txpower or args.cbr or args.prb_cap:
        # Command-line mode
        if args.qos:
            if not args.ue or args.percentage is None:
                print("Error: --qos requires --ue and --percentage")
                sys.exit(1)
            commands = [{"ueId": ue, "percentage": args.percentage} for ue in args.ue]
            send_config({"type": "qos", "commands": commands}, args.host, port)
        
        elif args.handover:
            if not args.imsi or not args.cell:
                print("Error: --handover requires --imsi and --cell")
                sys.exit(1)
            send_config({"type": "handover", "commands": [{"imsi": args.imsi, "targetCellId": args.cell}]}, args.host, port)
        
        elif args.energy:
            if not args.cell or args.ho_allowed is None:
                print("Error: --energy requires --cell and --ho-allowed")
                sys.exit(1)
            send_config({"type": "energy", "commands": [{"cellId": args.cell, "hoAllowed": args.ho_allowed}]}, args.host, port)
        
        elif args.enb_txpower:
            if not args.cell or args.dbm is None:
                print("Error: --enb-txpower requires --cell and --dbm")
                sys.exit(1)
            send_config({"type": "set-enb-txpower", "commands": [{"cellId": args.cell, "dbm": args.dbm}]}, args.host, port)
        
        elif args.ue_txpower:
            if not args.ue or args.dbm is None:
                print("Error: --ue-txpower requires --ue and --dbm")
                sys.exit(1)
            commands = [{"ueId": ue, "dbm": args.dbm} for ue in args.ue]
            send_config({"type": "set-ue-txpower", "commands": commands}, args.host, port)
        
        elif args.cbr:
            if not args.rate and not args.pkt_bytes:
                print("Error: --cbr requires --rate and/or --pkt-bytes")
                sys.exit(1)
            config = {"type": "set-cbr"}
            if args.rate:
                config["rate"] = args.rate
            if args.pkt_bytes:
                config["pktBytes"] = args.pkt_bytes
            send_config(config, args.host, port)
        
        elif args.prb_cap:
            if not args.ue or args.max_prb is None:
                print("Error: --prb-cap requires --ue and --max-prb")
                sys.exit(1)
            commands = [{"ueId": ue, "maxPrb": args.max_prb} for ue in args.ue]
            send_config({"type": "cap-ue-prb", "commands": commands}, args.host, port)
    else:
        # Interactive mode
        while True:
            show_menu()
            choice = input("\nSelect option: ").strip()
            
            if choice == "0":
                print("\nExiting...")
                break
            elif choice == "1":
                interactive_qos_config()
            elif choice == "2":
                interactive_handover_config()
            elif choice == "3":
                interactive_energy_config()
            elif choice == "4":
                interactive_enb_txpower_config()
            elif choice == "5":
                interactive_ue_txpower_config()
            elif choice == "6":
                interactive_cbr_config()
            elif choice == "7":
                interactive_prb_cap_config()
            else:
                print("Invalid option, please try again.")
            
            input("\nPress Enter to continue...")

if __name__ == "__main__":
    main()


#!/usr/bin/env python3
"""
Test script to manually trigger reactive control commands in the dummy AI server.

Usage:
    python3 test_reactive_control.py

This script sends commands to the dummy AI server via TCP (port 5002).
The dummy AI server must be running.
"""

import time
import sys
import socket
import json

def send_command_to_server(meid, cmd_dict, host="127.0.0.1", port=5002):
    """Send a command to the dummy AI server via TCP"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)  # 5 second timeout
        sock.connect((host, port))
        
        # Send command as JSON
        command = {
            "meid": meid,
            "cmd": cmd_dict
        }
        data = json.dumps(command).encode("utf-8")
        sock.sendall(data)
        
        # Receive response
        response = sock.recv(4096).decode("utf-8")
        sock.close()
        
        try:
            result = json.loads(response)
            return result
        except:
            return {"status": "ok", "message": response}
            
    except socket.timeout:
        return {"status": "error", "message": "Connection timeout - is the dummy AI server running?"}
    except ConnectionRefusedError:
        return {"status": "error", "message": f"Connection refused - is the dummy AI server running on {host}:{port}?"}
    except Exception as e:
        return {"status": "error", "message": str(e)}

def main():
    print("=" * 80)
    print("Reactive Control Command Test Script")
    print("=" * 80)
    print()
    print("This script will send test control commands to the dummy AI server.")
    print("Make sure the dummy AI server (ai_dummy_server.py) is running first!")
    print()
    
    # Test connection to command interface
    print("Testing connection to dummy AI server...")
    test_result = send_command_to_server("test", {"cmd": "test"}, port=5002)
    if test_result.get("status") == "error":
        print(f"❌ {test_result.get('message')}")
        print()
        print("Troubleshooting:")
        print("  1. Make sure ai_dummy_server.py is running")
        print("  2. Check that it's listening on port 5002")
        print("  3. Check the dummy AI server logs for errors")
        print()
        response = input("Continue anyway? (y/n): ").strip().lower()
        if response != 'y':
            print("Exiting...")
            return
    else:
        print("✅ Connected to dummy AI server")
    print()
    
    # Example MEIDs (adjust to match your setup)
    example_meids = [
        "gnb:131-133-31000000",
        "gnb:131-133-32000000",
        "gnb:131-133-33000000",
    ]
    
    print("Available test commands:")
    print("1. Set MCS (Modulation and Coding Scheme)")
    print("2. Set Bandwidth")
    print("3. Custom command")
    print()
    
    try:
        choice = input("Select command type (1-3) or 'q' to quit: ").strip()
        
        if choice.lower() == 'q':
            print("Exiting...")
            return
        
        meid = input(f"Enter MEID (or press Enter for default '{example_meids[0]}'): ").strip()
        if not meid:
            meid = example_meids[0]
        
        if choice == '1':
            # MCS command
            node = input("Enter node ID (default: 0): ").strip()
            node = int(node) if node else 0
            mcs = input("Enter MCS value (0-28, default: 10): ").strip()
            mcs = int(mcs) if mcs else 10
            
            cmd = {
                "cmd": "set-mcs",
                "node": node,
                "mcs": mcs
            }
            print(f"\nSending command: {cmd}")
            result = send_command_to_server(meid, cmd)
            if result.get("status") == "ok":
                print(f"✅ {result.get('message')}")
            else:
                print(f"❌ Error: {result.get('message')}")
            
        elif choice == '2':
            # Bandwidth command
            node = input("Enter node ID (default: 0): ").strip()
            node = int(node) if node else 0
            bandwidth = input("Enter bandwidth (default: 100): ").strip()
            bandwidth = int(bandwidth) if bandwidth else 100
            
            cmd = {
                "cmd": "set-bandwidth",
                "node": node,
                "bandwidth": bandwidth
            }
            print(f"\nSending command: {cmd}")
            result = send_command_to_server(meid, cmd)
            if result.get("status") == "ok":
                print(f"✅ {result.get('message')}")
            else:
                print(f"❌ Error: {result.get('message')}")
            
        elif choice == '3':
            # Custom command
            print("\nEnter custom command JSON (e.g., {\"cmd\":\"set-mcs\",\"node\":0,\"mcs\":5}):")
            cmd_str = input().strip()
            try:
                cmd = json.loads(cmd_str)
                print(f"\nSending command: {cmd}")
                result = send_command_to_server(meid, cmd)
                if result.get("status") == "ok":
                    print(f"✅ {result.get('message')}")
                else:
                    print(f"❌ Error: {result.get('message')}")
            except json.JSONDecodeError as e:
                print(f"Error: Invalid JSON - {e}")
                return
        else:
            print("Invalid choice!")
            return
        
        print("\n✅ Command sent! Check the dummy AI server and xApp logs to confirm.")
        print("   The xApp should receive and process the control command.")
        print("   Check ns-3 scenario logs for: [RicControlMessage] set-mcs: ...")
        
    except KeyboardInterrupt:
        print("\n\nExiting...")
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()


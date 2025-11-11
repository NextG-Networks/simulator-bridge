#!/usr/bin/env python3
import socket
import struct
import json

HOST = "0.0.0.0"
PORT = 5000
MAX_FRAME = 1024 * 1024  # 1MB

def recv_all(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data

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
            print(f"[AI-DUMMY] JSON: {text}")
            try:
                msg = json.loads(text)
                if msg.get("type") == "recommendation_request":
                    reply = json.dumps({"no_action": True}).encode("utf-8")
                    conn.sendall(struct.pack("!I", len(reply)) + reply)
            except Exception:
                # Non-JSON or malformed; just print and continue
                pass
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


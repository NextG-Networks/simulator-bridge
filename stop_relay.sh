#!/bin/bash
# Stop the AI Relay Server

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
RELAY_PID_FILE="${PROJECT_ROOT}/.relay_server.pid"

echo "Stopping AI Relay Server..."

KILLED_ANY=false

# Method 1: Use PID file if it exists
if [ -f "$RELAY_PID_FILE" ]; then
    PID=$(cat "$RELAY_PID_FILE" 2>/dev/null)
    if [ -n "$PID" ] && ps -p "$PID" > /dev/null 2>&1; then
        echo "Found relay server process (PID: $PID) from PID file"
        kill "$PID" 2>/dev/null || true
        sleep 1
        if ps -p "$PID" > /dev/null 2>&1; then
            echo "Force killing process $PID..."
            kill -9 "$PID" 2>/dev/null || true
        fi
        rm -f "$RELAY_PID_FILE"
        echo "✅ Stopped relay server (PID: $PID)"
        KILLED_ANY=true
    else
        echo "PID file exists but process is not running. Cleaning up PID file."
        rm -f "$RELAY_PID_FILE"
    fi
fi

# Method 2: Find processes by name (most reliable)
PIDS_BY_NAME=$(pgrep -f "ai_relay_server.py" 2>/dev/null || true)
if [ -n "$PIDS_BY_NAME" ]; then
    echo "Found relay server processes by name: $PIDS_BY_NAME"
    for pid in $PIDS_BY_NAME; do
        if ps -p "$pid" > /dev/null 2>&1; then
            echo "Killing relay server process (PID: $pid)..."
            kill "$pid" 2>/dev/null || true
            sleep 1
            if ps -p "$pid" > /dev/null 2>&1; then
                echo "Force killing process $pid..."
                kill -9 "$pid" 2>/dev/null || true
            fi
            echo "✅ Stopped relay server (PID: $pid)"
            KILLED_ANY=true
        fi
    done
fi

# Method 3: Find and kill processes using ports 5000 or 5002
if command -v lsof &> /dev/null; then
    # Check each port separately
    for port in 5000 5002; do
        PIDS=$(lsof -ti :$port 2>/dev/null || true)
        if [ -n "$PIDS" ]; then
            echo "Found processes using port $port: $PIDS"
            for pid in $PIDS; do
                if ps -p "$pid" > /dev/null 2>&1; then
                    CMD=$(ps -p "$pid" -o cmd= 2>/dev/null || echo "")
                    if echo "$CMD" | grep -q "ai_relay_server.py"; then
                        echo "Killing relay server process (PID: $pid) using port $port..."
                        kill "$pid" 2>/dev/null || true
                        sleep 1
                        if ps -p "$pid" > /dev/null 2>&1; then
                            echo "Force killing process $pid..."
                            kill -9 "$pid" 2>/dev/null || true
                        fi
                        echo "✅ Stopped relay server (PID: $pid)"
                        KILLED_ANY=true
                    else
                        echo "⚠️  Process $pid is using port $port but doesn't appear to be the relay server"
                        echo "   Command: $CMD"
                        if [ "$1" = "--force" ] || [ "$1" = "-f" ]; then
                            echo "   Force flag set, killing anyway..."
                            kill "$pid" 2>/dev/null || true
                            sleep 1
                            if ps -p "$pid" > /dev/null 2>&1; then
                                kill -9 "$pid" 2>/dev/null || true
                            fi
                            echo "✅ Killed process $pid"
                            KILLED_ANY=true
                        else
                            read -p "   Kill it anyway? (y/N): " -n 1 -r
                            echo
                            if [[ $REPLY =~ ^[Yy]$ ]]; then
                                kill "$pid" 2>/dev/null || true
                                sleep 1
                                if ps -p "$pid" > /dev/null 2>&1; then
                                    kill -9 "$pid" 2>/dev/null || true
                                fi
                                echo "✅ Killed process $pid"
                                KILLED_ANY=true
                            fi
                        fi
                    fi
                fi
            done
        fi
    done
elif command -v fuser &> /dev/null; then
    # Alternative using fuser
    echo "Using fuser to kill processes on ports 5000/5002..."
    fuser -k 5000/tcp 5002/tcp 2>/dev/null || true
    KILLED_ANY=true
fi

if [ "$KILLED_ANY" = false ]; then
    echo "No relay server processes found running."
fi

echo ""
echo "Done. You can now start the relay server with:"
echo "  python3 ai_relay_server.py"
echo "  or"
echo "  ./run_ai_relay.sh"


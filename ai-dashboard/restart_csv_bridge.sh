#!/bin/bash
# Quick restart script for CSV to InfluxDB bridge
# Usage: ./restart_csv_bridge.sh

# Dynamically determine script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

echo "[INFO] Stopping existing CSV bridge processes..."
pkill -9 -f csv_to_influxdb.py 2>/dev/null || true
rm -f .csv_bridge.pid
sleep 1

echo "[INFO] Starting CSV bridge..."

# Use venv if available, otherwise use system python
if [ -f "${SCRIPT_DIR}/venv/bin/python" ]; then
    PYTHON_CMD="${SCRIPT_DIR}/venv/bin/python"
elif [ -f "${SCRIPT_DIR}/dashboard_env/bin/python" ]; then
    PYTHON_CMD="${SCRIPT_DIR}/dashboard_env/bin/python"
else
    PYTHON_CMD="python3"
fi

$PYTHON_CMD -u csv_to_influxdb.py > csv_bridge.log 2>&1 &
BRIDGE_PID=$!
echo $BRIDGE_PID > .csv_bridge.pid

sleep 3

if ps -p $BRIDGE_PID > /dev/null 2>&1; then
    echo "[SUCCESS] CSV bridge started (PID: $BRIDGE_PID)"
    echo "[INFO] View logs: tail -f ${SCRIPT_DIR}/csv_bridge.log"
    echo ""
    echo "=== Recent Activity ==="
    tail -10 csv_bridge.log
else
    echo "[ERROR] CSV bridge failed to start"
    echo "=== Error Logs ==="
    cat csv_bridge.log
    exit 1
fi

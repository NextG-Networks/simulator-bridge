#!/bin/bash
# Script to check contents of NS3 control files

# Default control directory (can be overridden with NS3_CONTROL_DIR env var)
CONTROL_DIR=${NS3_CONTROL_DIR:-/tmp/ns3-control}

echo "=========================================="
echo "NS3 Control Files Check"
echo "=========================================="
echo "Control directory: $CONTROL_DIR"
echo ""

# Check if directory exists
if [ ! -d "$CONTROL_DIR" ]; then
    echo "❌ Control directory does not exist: $CONTROL_DIR"
    echo "   Make sure NS3_CONTROL_DIR is set correctly or files have been written."
    exit 1
fi

# List of control files
FILES=(
    "qos_actions.csv"
    "ts_actions_for_ns3.csv"
    "es_actions_for_ns3.csv"
    "enb_txpower_actions.csv"
    "ue_txpower_actions.csv"
    "cbr_actions.csv"
    "prb_cap_actions.csv"
)

# Check each file
for file in "${FILES[@]}"; do
    filepath="$CONTROL_DIR/$file"
    echo "----------------------------------------"
    echo "📄 $file"
    echo "----------------------------------------"
    
    if [ -f "$filepath" ]; then
        file_size=$(stat -f%z "$filepath" 2>/dev/null || stat -c%s "$filepath" 2>/dev/null || echo "unknown")
        if [ "$file_size" = "0" ] || [ "$file_size" = "unknown" ]; then
            echo "   Status: ⚪ Empty or cannot read size"
        else
            echo "   Status: ✅ Exists ($file_size bytes)"
            echo "   Contents:"
            echo "   ────────────────────────────────────────"
            cat "$filepath" | sed 's/^/   /'
            echo "   ────────────────────────────────────────"
        fi
    else
        echo "   Status: ❌ File does not exist"
    fi
    echo ""
done

echo "=========================================="
echo "Summary"
echo "=========================================="
echo "To view a specific file:"
echo "  cat $CONTROL_DIR/qos_actions.csv"
echo "  cat $CONTROL_DIR/ts_actions_for_ns3.csv"
echo "  cat $CONTROL_DIR/es_actions_for_ns3.csv"
echo "  cat $CONTROL_DIR/enb_txpower_actions.csv"
echo "  cat $CONTROL_DIR/ue_txpower_actions.csv"
echo "  cat $CONTROL_DIR/cbr_actions.csv"
echo "  cat $CONTROL_DIR/prb_cap_actions.csv"
echo ""
echo "To watch files in real-time:"
echo "  watch -n 1 'cat $CONTROL_DIR/qos_actions.csv'"
echo ""
echo "To check file modification times:"
echo "  ls -lht $CONTROL_DIR/*.csv"


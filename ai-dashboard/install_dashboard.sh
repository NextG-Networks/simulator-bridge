#!/bin/bash
# Install dashboard dependencies for Grafana + InfluxDB

echo "=========================================="
echo "NS3 KPI Dashboard - Installation"
echo "=========================================="
echo ""
echo "This installs dependencies for the Grafana dashboard solution."
echo ""

# Check if pip3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    exit 1
fi

# Check if we're in a virtual environment
if [ -n "$VIRTUAL_ENV" ]; then
    echo "✅ Virtual environment detected: $VIRTUAL_ENV"
    PIP_CMD="pip"
    PYTHON_CMD="python"
else
    echo "ℹ️  No virtual environment detected"
    echo "   Installing to user directory (--user flag)"
    echo "   Tip: Use a virtual environment for better isolation:"
    echo "     python3 -m venv venv"
    echo "     source venv/bin/activate"
    PIP_CMD="pip3"
    PYTHON_CMD="python3"
    USER_FLAG="--user"
fi

# Install InfluxDB bridge packages (required for Grafana)
echo ""
echo "Installing InfluxDB bridge packages..."
echo "  - influxdb-client: InfluxDB Python client"
echo "  - pandas: CSV data processing"
echo "  - watchdog: File system monitoring"
$PIP_CMD install $USER_FLAG influxdb-client pandas watchdog

echo ""
echo "✅ Installation complete!"
echo ""
if [ -n "$VIRTUAL_ENV" ]; then
    echo "Virtual environment: $VIRTUAL_ENV"
    echo ""
fi

echo "=========================================="
echo "Next Steps:"
echo "=========================================="
echo ""
echo "1. Set up Grafana + InfluxDB:"
echo "   ./setup_grafana.sh"
echo "   docker compose up -d"
echo ""
echo "2. Start the CSV to InfluxDB bridge:"
echo "   $PYTHON_CMD csv_to_influxdb.py"
echo ""
echo "3. Access Grafana dashboard:"
echo "   http://localhost:3000"
echo "   Login: admin / admin"
echo ""
echo "=========================================="
echo "Optional: Streamlit Dashboard (Deprecated)"
echo "=========================================="
echo ""
echo "Note: Streamlit dashboards are deprecated in favor of Grafana."
echo "If you need Streamlit for legacy reasons, install manually:"
echo "  $PIP_CMD install $USER_FLAG streamlit plotly"
echo ""
echo "See README_DASHBOARD.md for Grafana setup instructions."
echo ""

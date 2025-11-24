#!/bin/bash
# Install dashboard dependencies

echo "Installing KPI Dashboard dependencies..."
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

# Install Streamlit dashboard packages
echo ""
echo "Installing Streamlit dashboard packages..."
$PIP_CMD install $USER_FLAG streamlit pandas plotly watchdog

echo ""
echo "Installing InfluxDB bridge packages (optional, for Grafana)..."
$PIP_CMD install $USER_FLAG influxdb-client

echo ""
echo "✅ Installation complete!"
echo ""
if [ -n "$VIRTUAL_ENV" ]; then
    echo "Virtual environment: $VIRTUAL_ENV"
    echo ""
fi
echo "To run the Streamlit dashboard:"
echo "  ./run_dashboard.sh"
echo "  or"
echo "  $PYTHON_CMD -m streamlit run kpi_dashboard_realtime.py"
echo ""
echo "To set up Grafana + InfluxDB:"
echo "  ./setup_grafana.sh"
echo "  docker compose up -d"
echo ""
echo "To run the CSV to InfluxDB bridge:"
echo "  $PYTHON_CMD csv_to_influxdb.py"
echo ""


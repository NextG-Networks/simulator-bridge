#!/bin/bash
# Run the Real-Time KPI Dashboard

echo "Starting Real-Time KPI Dashboard..."
echo "Make sure CSV files (gnb_kpis.csv, ue_kpis.csv) are in the parent directory"
echo ""

# Detect Python command (use virtual env if active, otherwise python3)
if [ -n "$VIRTUAL_ENV" ]; then
    PYTHON_CMD="python"
    echo "✅ Using virtual environment: $VIRTUAL_ENV"
else
    PYTHON_CMD="python3"
fi

# Check if streamlit is installed
if ! $PYTHON_CMD -c "import streamlit" 2>/dev/null; then
    echo "Streamlit not found. Installing required packages..."
    if [ -n "$VIRTUAL_ENV" ]; then
        $PYTHON_CMD -m pip install streamlit pandas plotly watchdog
    else
        $PYTHON_CMD -m pip install --user streamlit pandas plotly watchdog
    fi
fi

# Run streamlit
$PYTHON_CMD -m streamlit run kpi_dashboard_realtime.py --server.port 8501 --server.address 0.0.0.0


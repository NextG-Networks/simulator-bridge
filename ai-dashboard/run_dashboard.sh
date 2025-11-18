#!/bin/bash
# Run the KPI Dashboard

echo "Starting KPI Dashboard..."
echo "Make sure CSV files (gnb_kpis.csv, ue_kpis.csv) are in the current directory"
echo ""

# Check if streamlit is installed
if ! python3 -c "import streamlit" 2>/dev/null; then
    echo "Installing required packages (user install)..."
    python3 -m pip install --user streamlit pandas plotly
fi

# Run streamlit (use python3 -m to ensure we use the right Python)
python3 -m streamlit run kpi_dashboard.py --server.port 8501 --server.address 0.0.0.0


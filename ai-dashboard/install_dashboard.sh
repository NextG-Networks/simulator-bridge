#!/bin/bash
# Install dashboard dependencies

echo "Installing KPI Dashboard dependencies..."
echo ""

# Check if pip3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    exit 1
fi

# Install using --user flag (recommended for PEP 668 protected systems)
echo "Installing packages to user directory..."
python3 -m pip install --user streamlit pandas plotly

echo ""
echo "✅ Installation complete!"
echo ""
echo "To run the dashboard:"
echo "  ./run_dashboard.sh"
echo "  or"
echo "  python3 -m streamlit run kpi_dashboard.py"
echo ""


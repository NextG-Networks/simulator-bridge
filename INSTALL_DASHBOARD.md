# Dashboard Installation Guide

## Recommended: pip install (Python packages)

**Use this method** - it's the standard way for Python packages:

```bash
# Install dependencies
pip3 install streamlit pandas plotly

# Or use the requirements file
pip3 install -r requirements_dashboard.txt
```

**Why pip?**

- ✅ Latest versions of packages
- ✅ All Python packages available
- ✅ Standard Python package management
- ✅ Works with virtual environments

---

## Alternative: apt install (if available)

Some packages might be available via apt, but **not recommended** for these packages:

```bash
# Check if available (usually not)
apt-cache search python3-streamlit python3-pandas python3-plotly

# If found, install (but versions will be older)
sudo apt update
sudo apt install python3-pandas python3-plotly
# Note: streamlit is usually NOT in apt repos
```

**Why not apt?**

- ❌ Streamlit is usually NOT in system repos
- ❌ Older versions (often 1-2 years behind)
- ❌ Missing features and bug fixes
- ❌ May not work with latest Python

---

## Using Virtual Environment (Recommended for isolation)

If you want to keep packages isolated:

```bash
# Create virtual environment
python3 -m venv dashboard_env

# Activate it
source dashboard_env/bin/activate

# Install packages
pip install streamlit pandas plotly

# Run dashboard
pip install streamlit pandas plotly

# Deactivate when done
deactivate
```

---

## Quick Install Script

```bash
#!/bin/bash
# Quick install script

echo "Installing dashboard dependencies..."

# Check if pip3 is available
if ! command -v pip3 &> /dev/null; then
    echo "Error: pip3 not found. Installing..."
    sudo apt update
    sudo apt install python3-pip
fi

# Install packages
pip3 install --user streamlit pandas plotly

echo "✅ Installation complete!"
echo "Run: streamlit run kpi_dashboard.py"
```

---

## Troubleshooting

### Permission errors with pip

```bash
# Use --user flag to install in user directory
pip3 install --user streamlit pandas plotly
```

### pip not found

```bash
sudo apt update
sudo apt install python3-pip
```

### Import errors after installation

```bash
# Make sure you're using the same Python that has packages
python3 -m pip install streamlit pandas plotly
python3 -m streamlit run kpi_dashboard.py
```

---

## Summary

**For this dashboard, use:**

```bash
pip3 install streamlit pandas plotly
```

This is the standard and recommended approach for Python packages.

# KPI Dashboard - Real-Time Visualization

A real-time web-based dashboard for visualizing KPIs from NS3 simulation.

> **NEW**: Enhanced real-time dashboard and Grafana integration available! See [README_DASHBOARD_REALTIME.md](README_DASHBOARD_REALTIME.md) for details.

## Features

- **Real-Time Updates**: Auto-refreshes every 0.5-5 seconds (configurable)
- **Cell-Level KPIs**: Visualize gNB/cell metrics
- **UE-Level KPIs**: Visualize per-UE metrics
- **Interactive Charts**: Plotly-based interactive visualizations
- **Data Tables**: View and download raw CSV data
- **Multi-Metric Views**: Compare multiple KPIs simultaneously

## Installation

```bash
# Install required packages
pip3 install -r requirements_dashboard.txt

# Or install manually
pip3 install streamlit pandas plotly
```

## Usage

### Quick Start

```bash
# Make sure CSV files are in the current directory
./run_dashboard.sh

# Or run directly
streamlit run kpi_dashboard.py
```

### Access the Dashboard

Once running, open your browser to:
- **Local**: http://localhost:8501
- **Network**: http://YOUR_IP:8501

### Real-Time Mode

1. **Enable Auto-Refresh**: Check "🔄 Real-Time Auto-Refresh" in the sidebar
2. **Set Refresh Interval**: Adjust slider (0.5-5 seconds)
3. **Monitor Live Status**: Check "Last Update" and "Latest Data" metrics

## Dashboard Tabs

### 1. Cell-Level (gNB)
- View cell-level KPIs over time
- Filter by Cell ID
- Multiple metrics on same chart
- Examples: PRB usage, TB counts, Mean active UEs, PDCP delay

### 2. UE-Level
- View per-UE KPIs
- Filter by UE ID and Cell ID
- Compare multiple UEs
- Examples: Throughput, BLER, PDCP delay, PRB usage

### 3. Combined View
- Latest values summary
- Correlation analysis
- Cross-metric comparisons

### 4. Data Tables
- View raw CSV data
- Download filtered data
- Export to CSV

## Real-Time Features

- **Live Indicator**: Shows refresh count and current time
- **Data Freshness**: Displays time since last data update
- **Auto-Refresh**: Continuously updates without manual refresh
- **Countdown Timer**: Shows time until next update

## Configuration

Edit `kpi_dashboard.py` to customize:

```python
CSV_GNB_FILE = "gnb_kpis.csv"  # gNB CSV file path
CSV_UE_FILE = "ue_kpis.csv"    # UE CSV file path
AUTO_REFRESH_INTERVAL = 1      # Default refresh interval (seconds)
```

## Troubleshooting

### No Data Showing
- Ensure CSV files exist in the same directory as `kpi_dashboard.py`
- Check that the simulation is running and generating data
- Verify CSV files have proper headers

### Slow Updates
- Reduce refresh interval in sidebar
- Check file I/O performance
- Consider using faster storage (SSD)

### Port Already in Use
```bash
# Use a different port
streamlit run kpi_dashboard.py --server.port 8502
```

## Example Use Cases

1. **Monitor Simulation Progress**: Watch KPIs update in real-time as simulation runs
2. **Debug Issues**: Identify anomalies in KPI values
3. **Performance Analysis**: Compare metrics across cells/UEs
4. **Data Export**: Download CSV for offline analysis

## Tips

- Use **Combined View** to see correlations between metrics
- Filter by specific UEs/Cells to focus on relevant data
- Download data periodically for backup
- Adjust refresh interval based on simulation speed (faster simulation = faster refresh)


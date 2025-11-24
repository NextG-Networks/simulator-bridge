# Real-Time KPI Dashboard - Enhanced

Enhanced real-time dashboard with true auto-refresh and Grafana integration for NS3 KPI visualization.

## Features

### Streamlit Dashboard (Improved)
- **True Real-Time Updates**: Auto-refreshes without page reloads
- **File Change Detection**: Only reloads when CSV files actually change
- **Preserved UI State**: Zoom/pan settings preserved during updates
- **Optimized Performance**: Short cache TTL and efficient data loading
- **Live Status Indicators**: Shows refresh count and data freshness

### Grafana Integration (New)
- **Time-Series Database**: InfluxDB for efficient time-series storage
- **Professional Dashboards**: Grafana for advanced visualization
- **Real-Time Streaming**: CSV data automatically synced to InfluxDB
- **Query Flexibility**: Use Flux queries for complex analysis
- **Alerting**: Set up alerts on KPI thresholds

## Installation

### Option 1: Virtual Environment (Recommended)

```bash
cd ai-dashboard

# Create and setup virtual environment
./setup_venv.sh

# Activate virtual environment
source venv/bin/activate

# Run the improved real-time dashboard
./run_dashboard_realtime.sh

# Or run the original dashboard
./run_dashboard.sh
```

### Option 2: System/User Installation

```bash
# Install dependencies (to user directory)
./install_dashboard.sh

# Run the improved real-time dashboard
./run_dashboard_realtime.sh

# Or run the original dashboard
./run_dashboard.sh
```

Access at: **http://localhost:8501**

### Option 3: Grafana + InfluxDB (Advanced)

```bash
# 1. Set up Grafana and InfluxDB
cd ai-dashboard
./setup_grafana.sh

# 2. Start the services (uses 'docker compose' or 'docker-compose' automatically)
docker compose up -d
# Or if you have standalone docker-compose:
# docker-compose up -d

# 3. Install Python bridge dependencies
# If using virtual environment:
source venv/bin/activate
pip install influxdb-client pandas watchdog

# Or system-wide:
pip3 install --user influxdb-client pandas watchdog

# 4. Configure environment variables (optional)
export INFLUXDB_URL="http://localhost:8086"
export INFLUXDB_TOKEN="my-super-secret-auth-token"
export INFLUXDB_ORG="ns3-org"
export INFLUXDB_BUCKET="ns3-kpis"

# 5. Run the CSV to InfluxDB bridge
# If using virtual environment:
source venv/bin/activate
python csv_to_influxdb.py

# Or system-wide:
python3 csv_to_influxdb.py
```

Access Grafana at: **http://localhost:3000**
- Username: `admin`
- Password: `admin`

Access InfluxDB at: **http://localhost:8086**
- Username: `admin`
- Password: `admin123456`

## Usage

### Streamlit Dashboard

1. **Start the dashboard**:
   ```bash
   ./run_dashboard_realtime.sh
   ```

2. **Enable Auto-Refresh**: Check "🔄 Real-Time Auto-Refresh" in sidebar

3. **Adjust Refresh Interval**: Use slider (0.5-5 seconds)

4. **Monitor Status**: Check "Last Update" and "Latest Data" metrics

The dashboard will automatically refresh when CSV files are updated, without requiring page reloads.

### Grafana Setup

1. **Start Services**:
   ```bash
   docker-compose up -d
   ```

2. **Run CSV Bridge** (in a separate terminal):
   ```bash
   python3 csv_to_influxdb.py
   ```

3. **Access Grafana**: Open http://localhost:3000

4. **Create Dashboards**:
   - Go to Dashboards → New Dashboard
   - Add panels with Flux queries
   - Example query:
     ```flux
     from(bucket: "ns3-kpis")
       |> range(start: -1h)
       |> filter(fn: (r) => r["_measurement"] == "gnb_kpis")
       |> filter(fn: (r) => r["_field"] == "prb_usage")
     ```

## Configuration

### Streamlit Dashboard

Edit `kpi_dashboard_realtime.py`:

```python
CSV_GNB_FILE = "../gnb_kpis.csv"  # gNB CSV file path
CSV_UE_FILE = "../ue_kpis.csv"    # UE CSV file path
AUTO_REFRESH_INTERVAL = 1.0        # Default refresh interval (seconds)
```

### InfluxDB Bridge

Set environment variables:

```bash
export INFLUXDB_URL="http://localhost:8086"
export INFLUXDB_TOKEN="my-super-secret-auth-token"
export INFLUXDB_ORG="ns3-org"
export INFLUXDB_BUCKET="ns3-kpis"
```

Or edit `csv_to_influxdb.py` directly.

## Grafana Dashboard Examples

### Example 1: gNB PRB Usage

```flux
from(bucket: "ns3-kpis")
  |> range(start: -1h)
  |> filter(fn: (r) => r["_measurement"] == "gnb_kpis")
  |> filter(fn: (r) => r["_field"] == "prb_usage")
  |> group(columns: ["cell_id"])
```

### Example 2: UE Throughput by Cell

```flux
from(bucket: "ns3-kpis")
  |> range(start: -1h)
  |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
  |> filter(fn: (r) => r["_field"] == "throughput")
  |> group(columns: ["ue_id", "cell_id"])
```

### Example 3: Multiple Metrics

```flux
from(bucket: "ns3-kpis")
  |> range(start: -1h)
  |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
  |> filter(fn: (r) => r["_field"] == "throughput" or r["_field"] == "bler")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
```

## Troubleshooting

### Streamlit Dashboard Not Updating

1. **Check CSV files exist**: Ensure `gnb_kpis.csv` and `ue_kpis.csv` are in the parent directory
2. **Check file permissions**: Ensure files are readable
3. **Check refresh interval**: Try reducing the interval (0.5s)
4. **Clear cache**: Click "🔄 Refresh Now" button

### InfluxDB Connection Issues

1. **Check InfluxDB is running**:
   ```bash
   docker compose ps
   # Or: docker-compose ps
   ```

2. **Check InfluxDB logs**:
   ```bash
   docker compose logs influxdb
   # Or: docker-compose logs influxdb
   ```

3. **Verify token**: Use the token from InfluxDB setup (default: `my-super-secret-auth-token`)

4. **Test connection**:
   ```bash
   curl http://localhost:8086/health
   ```

### Grafana Not Showing Data

1. **Check datasource**: Go to Configuration → Data Sources → InfluxDB
2. **Test connection**: Click "Test" button
3. **Check bucket name**: Ensure bucket is `ns3-kpis`
4. **Check time range**: Ensure query time range includes data

## Comparison: Streamlit vs Grafana

| Feature | Streamlit | Grafana |
|---------|-----------|---------|
| Setup | Simple (Python only) | Requires Docker |
| Real-Time | ✅ Auto-refresh | ✅ Real-time streaming |
| Customization | Limited | Extensive |
| Alerting | ❌ | ✅ |
| Query Language | Pandas | Flux (powerful) |
| Best For | Quick visualization | Production monitoring |

## Tips

1. **Use Streamlit for**: Quick development, simple dashboards, CSV analysis
2. **Use Grafana for**: Production monitoring, complex queries, alerting, long-term storage
3. **Run both**: Use Streamlit for development, Grafana for production
4. **Optimize refresh**: Lower refresh interval = more CPU usage
5. **Grafana alerts**: Set up alerts on critical KPIs (e.g., throughput < threshold)

## Next Steps

1. **Customize Grafana dashboards**: Create panels for your specific KPIs
2. **Set up alerts**: Configure Grafana alerts for threshold violations
3. **Add more data sources**: Integrate other monitoring tools
4. **Export dashboards**: Share Grafana dashboard JSON files


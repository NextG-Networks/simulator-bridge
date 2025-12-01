# NS3 KPI Dashboard - Grafana & InfluxDB

A professional real-time dashboard for visualizing KPIs from NS3 simulation using Grafana and InfluxDB.

## Quick Start

**New to this?** Start here: [START_TUTORIAL.md](START_TUTORIAL.md)

## Overview

This dashboard solution uses **Grafana** as the primary visualization platform, with **InfluxDB** as the time-series database. Data flows from NS3 simulation → CSV files → InfluxDB → Grafana for real-time monitoring and analysis.

> **Note**: Streamlit dashboards are deprecated. Use Grafana for production monitoring.

## Features

- **Real-Time Updates**: Auto-refreshes every 5 seconds (configurable)
- **Cell-Level KPIs**: Visualize gNB/cell metrics with filtering
- **UE-Level KPIs**: Visualize per-UE metrics with filtering
- **Interactive Charts**: Professional time-series visualizations
- **Variable Filtering**: Filter by Cell ID and UE ID dynamically
- **Multiple Panels**: Comprehensive view of all available KPIs
- **Historical Data**: Query and visualize historical data from InfluxDB

## Architecture

```
NS3 Simulation → CSV Files → CSV-to-InfluxDB Bridge → InfluxDB → Grafana Dashboard
```

## Installation

### 1. Install Dependencies

```bash
# Install Python dependencies for CSV bridge
cd ai-dashboard
./install_dashboard.sh
```

This installs:

- `influxdb-client` - For writing data to InfluxDB
- `pandas` - For CSV processing
- `watchdog` - For file monitoring

### 2. Setup Grafana and InfluxDB

```bash
# Run the setup script to create docker-compose.yml and configurations
./setup_grafana.sh

# Start Grafana and InfluxDB containers
docker compose up -d
```

This will:

- Create `docker-compose.yml` with Grafana and InfluxDB services
- Configure InfluxDB with default credentials
- Provision Grafana datasource and dashboard

### 3. Start CSV to InfluxDB Bridge

```bash
# In a separate terminal, start the bridge
cd ai-dashboard
python3 csv_to_influxdb.py
```

The bridge will:

- Watch for changes to `gnb_kpis.csv` and `ue_kpis.csv`
- Convert CSV data to InfluxDB points
- Write data to InfluxDB in real-time

## Usage

### Access Grafana Dashboard

1. **Start Services**:

   ```bash
   docker compose up -d
   ```
2. **Access Grafana**:

   - Open browser to: http://localhost:3000
   - Login: `admin` / `admin` (change on first login)
3. **View Dashboard**:

   - Navigate to "Dashboards" → "NS3 KPI Dashboard"
   - Or access directly: http://localhost:3000/d/ns3-kpis-dashboard

### Dashboard Features

#### Summary Panels

- **Latest Data Timestamp**: Shows when data was last received
- **Active Cells**: Count of active cells in the last 5 minutes
- **Active UEs**: Count of active UEs in the last 5 minutes

#### Cell-Level (gNB) Panels

- **PRB Usage (DL)**: Physical Resource Block usage over time
- **Mean Active UEs (DL)**: Average number of active UEs
- **Transport Block Counts**: QPSK and 64QAM modulation counts
- **PDCP Delay (DL)**: Packet delay in milliseconds

#### UE-Level Panels

- **Throughput (DL)**: Per-UE downlink throughput in Mbps
- **PRB Usage (DL)**: Per-UE resource block allocation
- **PDCP Delay (DL)**: Per-UE packet delay
- **Transport Block Counts**: Per-UE modulation statistics

#### Filtering

- **Cell ID**: Dropdown to filter by specific cell(s)
- **UE ID**: Dropdown to filter by specific UE(s)
- **Time Range**: Select time window (default: last 1 hour)

## Configuration

### InfluxDB Settings

Default configuration (in `csv_to_influxdb.py`):

```python
INFLUXDB_URL = "http://localhost:8086"
INFLUXDB_TOKEN = "my-super-secret-auth-token"
INFLUXDB_ORG = "ns3-org"
INFLUXDB_BUCKET = "ns3-kpis"
```

To customize, set environment variables:

```bash
export INFLUXDB_URL="http://your-influxdb:8086"
export INFLUXDB_TOKEN="your-token"
export INFLUXDB_ORG="your-org"
export INFLUXDB_BUCKET="your-bucket"
```

### CSV File Paths

Default paths (in `csv_to_influxdb.py`):

```python
CSV_GNB_FILE = "../gnb_kpis.csv"
CSV_UE_FILE = "../ue_kpis.csv"
```

### Grafana Dashboard

Dashboard configuration is in:

- `grafana/dashboards/ns3-kpis.json` - Dashboard definition
- `grafana/provisioning/datasources/influxdb.yml` - InfluxDB connection

## Troubleshooting

### Verify Setup

First, run the verification script to check your setup:

```bash
cd ai-dashboard
python3 verify_setup.py
```

This will check:
- InfluxDB connection
- Bucket existence
- Measurements (data) in InfluxDB
- Recent data points
- CSV file existence

### Dashboard Not Appearing in Grafana

1. **Restart Grafana** to pick up the dashboard:
   ```bash
   docker compose restart grafana
   ```

2. **Check Dashboard File**:
   - Verify `grafana/dashboards/ns3-kpis.json` exists
   - Check JSON is valid: `python3 -m json.tool grafana/dashboards/ns3-kpis.json`

3. **Check Grafana Logs**:
   ```bash
   docker compose logs grafana | grep -i dashboard
   ```

4. **Manually Import Dashboard** (if provisioning fails):
   - In Grafana: Dashboards → Import
   - Upload `grafana/dashboards/ns3-kpis.json`
   - Or paste the JSON content

5. **Check Provisioning**:
   - Verify `grafana/provisioning/dashboards/default.yml` exists
   - Check path is correct: `/var/lib/grafana/dashboards`

### No Data in Grafana

1. **Check InfluxDB Connection**:
   ```bash
   # Verify InfluxDB is running
   docker ps | grep influxdb
   
   # Check InfluxDB UI
   open http://localhost:8086
   ```

2. **Verify CSV Bridge**:
   - Check that `csv_to_influxdb.py` is running
   - Verify CSV files exist and are being updated
   - Check bridge logs for errors
   - Make sure bridge is in the `ai-dashboard` directory

3. **Check Data in InfluxDB**:
   - Access InfluxDB UI at http://localhost:8086
   - Login: `admin` / `admin123456`
   - Navigate to Data Explorer
   - Query: `from(bucket: "ns3-kpis") |> range(start: -1h)`
   - Or run: `python3 verify_setup.py`

4. **Check Measurements Exist**:
   ```bash
   # Using verification script
   python3 verify_setup.py
   
   # Or manually query InfluxDB
   # In InfluxDB UI Data Explorer:
   import "influxdata/influxdb/schema"
   schema.measurements(bucket: "ns3-kpis")
   ```

### Dashboard Not Loading

1. **Check Grafana Logs**:
   ```bash
   docker compose logs grafana
   ```
2. **Verify Dashboard File**:

   - Check `grafana/dashboards/ns3-kpis.json` exists
   - Verify JSON is valid
3. **Check Datasource**:

   - In Grafana: Configuration → Data Sources
   - Verify "InfluxDB" datasource is configured
   - Test connection

### CSV Bridge Not Writing Data

1. **Check InfluxDB Token**:

   - Verify token matches in `csv_to_influxdb.py` and `docker-compose.yml`
   - Default: `my-super-secret-auth-token`
2. **Check Organization and Bucket**:

   - Verify org: `ns3-org`
   - Verify bucket: `ns3-kpis`
3. **Check CSV File Paths**:

   - Verify CSV files exist at configured paths
   - Check file permissions

## Available KPIs

### Cell-Level (gNB) Metrics

- `RRU_PrbUsedDl` - PRB usage (downlink)
- `DRB_MeanActiveUeDl` - Mean active UEs
- `TB_TotNbrDlInitial_Qpsk` - QPSK transport blocks
- `TB_TotNbrDlInitial_64Qam` - 64QAM transport blocks
- `DRB_PdcpSduDelayDl` - PDCP delay

### UE-Level Metrics

- `DRB_UEThpDl_UEID` - UE throughput (Mbps)
- `RRU_PrbUsedDl_UEID` - UE PRB usage
- `DRB_PdcpSduDelayDl_UEID` - UE PDCP delay
- `TB_TotNbrDlInitial_Qpsk_UEID` - UE QPSK transport blocks
- `TB_TotNbrDlInitial_64Qam_UEID` - UE 64QAM transport blocks

## Advanced Usage

### Custom Queries

You can create custom panels in Grafana using Flux queries:

```flux
from(bucket: "ns3-kpis")
  |> range(start: -1h)
  |> filter(fn: (r) => r["_measurement"] == "gnb_kpis")
  |> filter(fn: (r) => r["_field"] == "RRU_PrbUsedDl")
  |> aggregateWindow(every: 1m, fn: mean)
```

### Exporting Data

1. **From Grafana**: Use panel menu → "Explore" → Export
2. **From InfluxDB**: Use InfluxDB UI Data Explorer → Export
3. **From CSV**: Original CSV files remain available

### Performance Tuning

- **Reduce Refresh Rate**: Change dashboard refresh from 5s to 10s or 30s
- **Limit Time Range**: Use shorter time ranges for faster queries
- **Aggregate Windows**: Use larger aggregation windows (e.g., 1m instead of 5s)

## Verification

After setup, verify everything is working:

```bash
cd ai-dashboard
python3 verify_setup.py
```

This script checks:
- InfluxDB connectivity
- Bucket existence
- Data measurements
- Recent data points
- CSV file availability

**For first-time setup, follow [START_TUTORIAL.md](START_TUTORIAL.md)**

## Stopping Services

```bash
# Stop containers
docker compose down

# Stop CSV bridge
# Press Ctrl+C in the terminal running csv_to_influxdb.py
```

## Migration from Streamlit

If you were using Streamlit dashboards:

1. **Stop Streamlit**: No longer needed
2. **Start Grafana**: Follow installation steps above
3. **Data Migration**: Historical CSV data can be imported to InfluxDB if needed
4. **Remove Streamlit**: Optional - can keep for reference

## Support

For issues or questions:

- Check logs: `docker compose logs`
- Verify configuration matches between bridge and InfluxDB
- Ensure CSV files are being generated by NS3 simulation

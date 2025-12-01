# Getting Started with InfluxDB and Grafana

A step-by-step tutorial to set up and use the NS3 KPI Dashboard with InfluxDB and Grafana.

## Prerequisites

- Docker and Docker Compose installed
- Python 3.8+ installed
- CSV files from NS3 simulation (`gnb_kpis.csv` and `ue_kpis.csv`)

## Step 1: Install Dependencies

```bash
cd ai-dashboard
./install_dashboard.sh
```

This installs:
- `influxdb-client` - Python client for InfluxDB
- `pandas` - CSV data processing
- `watchdog` - File system monitoring

**Optional**: Use a virtual environment:
```bash
python3 -m venv venv
source venv/bin/activate
./install_dashboard.sh
```

## Step 2: Set Up InfluxDB and Grafana

### 2.1 Run Setup Script

```bash
cd ai-dashboard
./setup_grafana.sh
```

This creates:
- `docker-compose.yml` - Docker configuration
- Grafana datasource configuration
- Dashboard provisioning files

### 2.2 Start Services

```bash
docker compose up -d
```

This starts:
- **InfluxDB** on port 8086
- **Grafana** on port 3000

### 2.3 Verify Services Are Running

```bash
docker compose ps
```

You should see both containers with status "Up" and "(healthy)".

**Check InfluxDB**:
```bash
curl http://localhost:8086/health
```

Should return: `{"status":"pass",...}`

**Check Grafana**:
```bash
curl http://localhost:3000/api/health
```

Should return: `{"database":"ok",...}`

## Step 3: Import Existing CSV Data

If you have existing CSV files with data, import them into InfluxDB:

```bash
cd ai-dashboard
source venv/bin/activate  # if using venv
python3 import_csv_data.py
```

This will:
- Read all data from `gnb_kpis.csv` and `ue_kpis.csv`
- Convert to InfluxDB format
- Write to InfluxDB bucket `ns3-kpis`

**Expected output**:
```
✅ Imported X gNB rows
✅ Imported Y UE rows
```

## Step 4: Verify Setup

Run the verification script:

```bash
python3 verify_setup.py
```

**Expected output**:
```
✅ CSV files: OK
✅ InfluxDB bucket: OK
✅ Measurements: Found
✅ Recent data: Found
```

If you see any ❌, check the troubleshooting section below.

## Step 5: Access Grafana Dashboard

### 5.1 Open Grafana

1. Open browser: http://localhost:3000
2. Login:
   - Username: `admin`
   - Password: `admin`
3. Change password when prompted (optional)

### 5.2 Import Dashboard

The dashboard should appear automatically. If not:

1. Go to **Dashboards** → **Import**
2. Click **Upload JSON file**
3. Select: `ai-dashboard/grafana/dashboards/ns3-kpis.json`
4. Select datasource: **InfluxDB**
5. Click **Import**

### 5.3 View Dashboard

1. Navigate to **Dashboards** → **NS3 KPI Dashboard**
2. **Adjust time range** (top right):
   - Default: "Last 1 hour"
   - If no data: Try "Last 24 hours" or "Last 7 days"
3. You should see:
   - **gNB KPIs** panel (left)
   - **UE KPIs** panel (right)

## Step 6: Start Real-Time Updates

To get live updates as new data arrives:

```bash
cd ai-dashboard
source venv/bin/activate  # if using venv
python3 csv_to_influxdb.py
```

This script:
- Watches for changes to CSV files
- Automatically writes new data to InfluxDB
- Runs continuously until you press `Ctrl+C`

**Keep this running** while your NS3 simulation is generating data.

## Step 7: Access InfluxDB UI (Optional)

You can also view data directly in InfluxDB:

1. Open: http://localhost:8086
2. Login:
   - Username: `admin`
   - Password: `admin123456`
3. Navigate to **Data Explorer**
4. Select bucket: `ns3-kpis`
5. Write Flux queries to explore data

## Common Tasks

### View Data in InfluxDB

**Query all gNB data**:
```flux
from(bucket: "ns3-kpis")
  |> range(start: -24h)
  |> filter(fn: (r) => r["_measurement"] == "gnb_kpis")
```

**Query specific UE**:
```flux
from(bucket: "ns3-kpis")
  |> range(start: -24h)
  |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
  |> filter(fn: (r) => r["ue_id"] == "3030303031")
```

### Stop Services

```bash
docker compose down
```

### Restart Services

```bash
docker compose restart
```

### View Logs

**Grafana logs**:
```bash
docker compose logs grafana
```

**InfluxDB logs**:
```bash
docker compose logs influxdb
```

## Troubleshooting

### Services Won't Start

**Check Docker**:
```bash
docker ps
docker compose ps
```

**Check ports**:
```bash
# Check if ports are in use
netstat -tuln | grep 3000
netstat -tuln | grep 8086
```

**Restart services**:
```bash
docker compose down
docker compose up -d
```

### No Data in Dashboard

1. **Check time range**: Change to "Last 24 hours" or "Last 7 days"
2. **Verify data exists**:
   ```bash
   python3 verify_setup.py
   ```
3. **Import data** (if not done):
   ```bash
   python3 import_csv_data.py
   ```
4. **Check InfluxDB UI**: http://localhost:8086 → Data Explorer

### Dashboard Not Appearing

1. **Check dashboard file**:
   ```bash
   ls -la grafana/dashboards/ns3-kpis.json
   python3 -m json.tool grafana/dashboards/ns3-kpis.json
   ```
2. **Restart Grafana**:
   ```bash
   docker compose restart grafana
   ```
3. **Manually import** (see Step 5.2)

### CSV Bridge Not Writing Data

1. **Check InfluxDB is running**:
   ```bash
   docker compose ps influxdb
   ```
2. **Check CSV files exist**:
   ```bash
   ls -la ../gnb_kpis.csv ../ue_kpis.csv
   ```
3. **Check bridge logs** for errors
4. **Verify configuration** in `csv_to_influxdb.py`:
   - INFLUXDB_URL
   - INFLUXDB_TOKEN
   - INFLUXDB_ORG
   - INFLUXDB_BUCKET

## Next Steps

- **Customize dashboard**: Edit `grafana/dashboards/ns3-kpis.json`
- **Add alerts**: Set up Grafana alerts on KPI thresholds
- **Export data**: Use InfluxDB UI or Grafana to export data
- **Create custom queries**: Write Flux queries for specific analysis

## Quick Reference

| Service | URL | Credentials |
|---------|-----|-------------|
| Grafana | http://localhost:3000 | admin / admin |
| InfluxDB | http://localhost:8086 | admin / admin123456 |

| Command | Description |
|---------|-------------|
| `docker compose up -d` | Start services |
| `docker compose down` | Stop services |
| `docker compose restart` | Restart services |
| `python3 import_csv_data.py` | Import CSV data |
| `python3 csv_to_influxdb.py` | Start real-time bridge |
| `python3 verify_setup.py` | Verify setup |

## Architecture Overview

```
┌─────────────────┐
│  NS3 Simulation │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   CSV Files     │
│ gnb_kpis.csv    │
│  ue_kpis.csv    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ CSV-to-InfluxDB │
│     Bridge      │
│ (csv_to_influx  │
│    _db.py)      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│    InfluxDB     │
│  (Time-Series   │
│    Database)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Grafana     │
│   (Dashboard)   │
└─────────────────┘
```

## Support

For more information, see:
- `README_DASHBOARD.md` - Complete documentation
- `explain_tags.md` - Understanding InfluxDB tags
- Grafana documentation: https://grafana.com/docs/
- InfluxDB documentation: https://docs.influxdata.com/


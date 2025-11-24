# Quick Start Guide - Grafana Dashboard

## Current Status

Based on your verification output:
- ✅ CSV files exist
- ✅ InfluxDB connection works
- ✅ Measurements exist (gnb_kpis, ue_kpis)
- ⚠️  No recent data (data may be older than 1 hour)
- ⚠️  Dashboard not appearing in Grafana

## Step 1: Import Existing CSV Data

Your CSV files have data, but it hasn't been imported to InfluxDB yet. Run:

```bash
cd ai-dashboard
source venv/bin/activate  # if using venv
python3 import_csv_data.py
```

This will import all existing data from your CSV files into InfluxDB.

## Step 2: Verify Data Import

After importing, verify the data:

```bash
python3 verify_setup.py
```

You should now see:
- ✅ Recent data: Found

## Step 3: Fix Dashboard in Grafana

The dashboard may not appear automatically. Try these options:

### Option A: Restart Grafana (if not done already)
```bash
cd ai-dashboard
docker compose restart grafana
```

Wait 30 seconds, then check: http://localhost:3000

### Option B: Manually Import Dashboard

1. Open Grafana: http://localhost:3000
2. Login: `admin` / `admin`
3. Go to: **Dashboards** → **Import**
4. Click **Upload JSON file**
5. Select: `ai-dashboard/grafana/dashboards/ns3-kpis.json`
6. Click **Load**
7. Select datasource: **InfluxDB**
8. Click **Import**

### Option C: Check Dashboard Provisioning

If the dashboard still doesn't appear, check Grafana logs:

```bash
docker compose logs grafana | grep -i dashboard
```

Look for any errors about dashboard provisioning.

## Step 4: Start Real-Time Updates

To get real-time updates as new data arrives:

```bash
cd ai-dashboard
source venv/bin/activate  # if using venv
python3 csv_to_influxdb.py
```

This will:
- Watch for CSV file changes
- Automatically write new data to InfluxDB
- Keep running until you press Ctrl+C

## Troubleshooting

### Dashboard shows "No data"

1. Check time range in dashboard (top right) - try "Last 30 days" or "Last 7 days"
2. Verify data exists: Run `python3 verify_setup.py`
3. Check if measurements have data in InfluxDB UI: http://localhost:8086

### Dashboard not appearing

1. Check dashboard file exists: `ls -la grafana/dashboards/ns3-kpis.json`
2. Check JSON is valid: `python3 -m json.tool grafana/dashboards/ns3-kpis.json`
3. Manually import (see Step 3, Option B above)

### No data in InfluxDB

1. Run the import script: `python3 import_csv_data.py`
2. Check CSV files exist and have data
3. Verify InfluxDB is running: `docker ps | grep influxdb`

## Next Steps

Once everything is working:

1. **Keep csv_to_influxdb.py running** for real-time updates
2. **Access dashboard**: http://localhost:3000/d/ns3-kpis-dashboard
3. **Adjust time range** as needed in the dashboard
4. **Filter by Cell/UE** using the dropdowns (if you add them back)


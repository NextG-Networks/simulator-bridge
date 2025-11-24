# Grafana Startup Issue - FIXED

## Problem
Grafana was failing to start with error:
```
Datasource provisioning error: data source not found
```

## Root Cause
The datasource configuration had a `uid: InfluxDB` field that was causing Grafana to fail during provisioning.

## Solution Applied
1. Removed `uid` field from datasource configuration
2. Added `version: 1` to datasource configuration
3. Removed obsolete `version: '3.8'` from docker-compose.yml
4. Added healthchecks for better container management

## Current Status
✅ **Grafana is now running and healthy!**

## Access Grafana
- URL: http://localhost:3000
- Username: `admin`
- Password: `admin`

## Next Steps

1. **Import your CSV data** (if not done already):
   ```bash
   cd ai-dashboard
   source venv/bin/activate  # if using venv
   python3 import_csv_data.py
   ```

2. **Import the dashboard**:
   - Go to http://localhost:3000
   - Login with admin/admin
   - Navigate to: **Dashboards** → **Import**
   - Upload: `grafana/dashboards/ns3-kpis.json`
   - Select datasource: **InfluxDB**
   - Click **Import**

3. **Start real-time updates**:
   ```bash
   python3 csv_to_influxdb.py
   ```

## Files Modified
- `grafana/provisioning/datasources/influxdb.yml` - Removed `uid` field
- `docker-compose.yml` - Removed version, improved healthchecks
- `grafana/dashboards/ns3-kpis.json` - Updated datasource references


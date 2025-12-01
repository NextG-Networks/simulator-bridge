# NS3 KPI Dashboard

Real-time visualization dashboard for NS3 simulation KPIs using Grafana and InfluxDB.

## Documentation

- **[START_TUTORIAL.md](START_TUTORIAL.md)** - **Start here!** Complete step-by-step setup guide
- **[README_DASHBOARD.md](README_DASHBOARD.md)** - Complete documentation and reference
- **[QUICK_START.md](QUICK_START.md)** - Quick setup reference
- **[explain_tags.md](explain_tags.md)** - Technical explanation of InfluxDB tags

## Quick Start

### Automated Deployment (Recommended)

```bash
cd ai-dashboard
./deploy_dashboard.sh
```

This automatically:
- Installs dependencies
- Sets up Grafana and InfluxDB
- Imports CSV data (if available)
- Starts the CSV bridge

**Options:**
- `--skip-install` - Skip dependency installation
- `--skip-setup` - Skip Grafana/InfluxDB setup
- `--skip-import` - Skip CSV data import
- `--skip-bridge` - Skip starting CSV bridge
- `--background` - Run CSV bridge in background
- `--non-interactive` or `-y` - Non-interactive mode

### Manual Setup

```bash
# 1. Install dependencies
./install_dashboard.sh

# 2. Set up and start services
./setup_grafana.sh
docker compose up -d

# 3. Import CSV data (if available)
python3 import_csv_data.py

# 4. Access Grafana
# http://localhost:3000 (admin/admin)
```

For detailed instructions, see [START_TUTORIAL.md](START_TUTORIAL.md).

## Services

- **Grafana**: http://localhost:3000 (admin/admin)
- **InfluxDB**: http://localhost:8086 (admin/admin123456)

## Architecture

```
NS3 Simulation → CSV Files → CSV-to-InfluxDB Bridge → InfluxDB → Grafana Dashboard
```

## Files

### Scripts
- `install_dashboard.sh` - Install Python dependencies
- `setup_grafana.sh` - Set up Grafana and InfluxDB
- `import_csv_data.py` - Import existing CSV data to InfluxDB
- `csv_to_influxdb.py` - Real-time CSV to InfluxDB bridge
- `verify_setup.py` - Verify setup and data

### Configuration
- `docker-compose.yml` - Docker services configuration
- `grafana/` - Grafana dashboards and provisioning
- `requirements_dashboard.txt` - Python dependencies


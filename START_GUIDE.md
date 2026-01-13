# Simulator Bridge - Start Guide

This guide shows you how to start the entire simulator bridge system, including both **automatic** (using shell scripts) and **manual** (step-by-step) methods.

> **Note**: All scripts use dynamic path detection, so you can clone this project to any directory. The scripts automatically detect the project root based on their location.

## Table of Contents

- [System Overview](#system-overview)
- [Prerequisites](#prerequisites)
- [Automatic Startup (Recommended)](#automatic-startup-recommended)
- [Manual Startup](#manual-startup)
- [Optional: AI Dashboard](#optional-ai-dashboard)
- [Verification](#verification)
- [Troubleshooting](#troubleshooting)
- [Stopping the System](#stopping-the-system)

---

## System Overview

The simulator bridge consists of the following components:

1. **RIC (RAN Intelligent Controller) Components**
   - Database container (`db`)
   - E2 Termination (`e2term`)
   - E2 Manager (`e2mgr`)
   - E2 RAN Traffic Manager Simulator (`e2rtmansim`)

2. **xApp Container**
   - Sample xApp (`sample-xapp-24`) - connects to RIC and AI relay

3. **AI Relay Server**
   - Python service that bridges xApp and external AI server
   - Listens on ports 5000 (xApp) and 5002 (command interface)
   - Forwards to external AI on port 6000

4. **NS-3 Simulation**
   - Network simulator running O-RAN scenarios
   - Generates KPI data and communicates with RIC

5. **AI Dashboard (Optional)**
   - Grafana for visualization
   - InfluxDB for time-series data storage
   - CSV bridge for real-time data import

---

## Prerequisites

Before starting, ensure you have:

- **Docker** installed and running
- **Python 3** installed
- **Docker images** imported (handled automatically by scripts)
- **Network access** to external AI server (if using remote AI)

Check Docker:
```bash
docker info
```

Check Python:
```bash
python3 --version
```

---

## Automatic Startup (Recommended)

The easiest way to start everything is using the automated deployment script.

### Quick Start

```bash
# Navigate to the project root directory
cd <path-to-simulator-bridge>
./deploy.sh
```

**Note**: The script automatically detects the project root, so you can run it from anywhere within the project directory.

This will:
1. ✅ Import Docker images (if needed)
2. ✅ Set up RIC containers (db, e2term, e2mgr, e2rtmansim)
3. ✅ Start AI Relay Server
4. ✅ Build and start xApp container
5. ✅ Start NS-3 simulation (with scenario selection)

### Advanced Options

```bash
# Skip specific components
./deploy.sh --skip-import      # Skip Docker image import
./deploy.sh --skip-ric         # Skip RIC setup
./deploy.sh --skip-xapp        # Skip xApp setup
./deploy.sh --skip-ns3         # Skip NS-3 simulation
./deploy.sh --skip-relay       # Skip AI Relay Server

# Run NS-3 in background
./deploy.sh --background

# Non-interactive mode (use defaults)
./deploy.sh --non-interactive
# or
./deploy.sh -y

# Specify NS-3 scenario
./deploy.sh --scenario ourv2.cc

# Specify external AI server IP
./deploy.sh --ai-ip 130.240.5.17

# Combine options
./deploy.sh --skip-import --background --scenario ourv2.cc
```

### What the Script Does

1. **Pre-flight Checks**: Verifies Docker, Python, and required files
2. **Import Images**: Imports Wines Lab Docker images (if not already imported)
3. **Setup RIC**: Starts RIC Bronze containers
4. **Start Relay**: Launches AI Relay Server in background
5. **Setup xApp**: Builds and starts sample xApp container
6. **Run NS-3**: Executes NS-3 simulation with selected scenario

---

## Manual Startup

If you prefer to start components manually or need more control, follow these steps:

### Step 1: Import Docker Images (First Time Only)

```bash
# From the project root
cd colosseum-near-rt-ric/setup-scripts
./import-wines-images.sh
```

This imports the base RIC images:
- `e2term:bronze`
- `e2mgr:bronze`
- `e2rtmansim:bronze`
- `dbaas:bronze`

**Note**: Skip this step if images are already imported.

### Step 2: Setup RIC Containers

```bash
# From the project root
cd colosseum-near-rt-ric/setup-scripts
./setup-ric-bronze.sh
```

This starts:
- `db` - Database container
- `e2term` - E2 Termination
- `e2mgr` - E2 Manager
- `e2rtmansim` - E2 RAN Traffic Manager Simulator

**Verify containers are running:**
```bash
docker ps --filter "name=db" --filter "name=e2term" --filter "name=e2mgr" --filter "name=e2rtmansim"
```

### Step 3: Start AI Relay Server

**Option A: Using the shell script**
```bash
# From the project root
export EXTERNAL_AI_HOST=130.240.5.17  # Optional: set external AI IP
export EXTERNAL_AI_PORT=6000          # Optional: set external AI port
./run_ai_relay.sh
```

**Option B: Direct Python execution**
```bash
# From the project root
export EXTERNAL_AI_HOST=130.240.5.17  # Optional
export EXTERNAL_AI_PORT=6000          # Optional
python3 ai_relay_server.py
```

**Option C: Run in background**
```bash
# From the project root
export EXTERNAL_AI_HOST=130.240.5.17
nohup python3 ai_relay_server.py > relay_server.log 2>&1 &
echo $! > .relay_server.pid
```

**Verify relay server is running:**
```bash
# Check if process is running
ps aux | grep ai_relay_server.py

# Check if ports are listening
lsof -i :5000 -i :5002

# View logs (if running in background)
tail -f relay_server.log
```

### Step 4: Setup and Start xApp

```bash
# From the project root
cd colosseum-near-rt-ric/setup-scripts

# Build and start xApp container
./setup-sample-xapp.sh ns-o-ran

# Start xApp inside the container
docker exec -d sample-xapp-24 bash -c "cd /home/sample-xapp && ./run_xapp.sh"
```

**Verify xApp is running:**
```bash
# Check container status
docker ps --filter "name=sample-xapp-24"

# Check xApp process inside container
docker exec sample-xapp-24 pgrep -f run_xapp.py

# View xApp logs
docker logs sample-xapp-24
```

### Step 5: Run NS-3 Simulation

```bash
# From the project root
cd ns-3-mmwave-oran

# List available scenarios
ls scratch/*.cc

# Run a specific scenario
./ns3 run scratch/ourv2.cc

# Or run in background
nohup ./ns3 run scratch/ourv2.cc > ns3_simulation.log 2>&1 &
```

**Common scenarios:**
- `ourv2.cc` - Default scenario
- `scenario_two.cc` - Alternative scenario
- `scenario_three.cc` - Alternative scenario

---

## Optional: AI Dashboard

The AI Dashboard provides real-time visualization of KPI data using Grafana and InfluxDB.

### Automatic Dashboard Setup

```bash
# From the project root
cd ai-dashboard
./deploy_dashboard.sh
```

This will:
1. Install Python dependencies
2. Set up Grafana and InfluxDB
3. Import existing CSV data (if available)
4. Start CSV bridge for real-time updates

### Manual Dashboard Setup

```bash
# From the project root
cd ai-dashboard

# 1. Install dependencies
./install_dashboard.sh

# 2. Setup Grafana and InfluxDB
./setup_grafana.sh
docker compose up -d

# 3. Import CSV data (if you have existing data)
source venv/bin/activate  # if using venv
python3 import_csv_data.py

# 4. Start real-time CSV bridge
python3 csv_to_influxdb.py
```

**Access Dashboard:**
- Grafana: http://localhost:3000 (admin/admin)
- InfluxDB: http://localhost:8086 (admin/admin123456)

---

## Verification

### Check All Components

```bash
# All commands can be run from anywhere

# 1. Check RIC containers
docker ps --filter "name=db" --filter "name=e2term" --filter "name=e2mgr" --filter "name=e2rtmansim"

# 2. Check xApp container
docker ps --filter "name=sample-xapp-24"

# 3. Check AI Relay Server
ps aux | grep ai_relay_server.py
lsof -i :5000 -i :5002

# 4. Check NS-3 (if running in background)
ps aux | grep ns3

# 5. Check Dashboard (if running)
docker ps --filter "name=ns3-grafana" --filter "name=ns3-influxdb"
```

### View Logs

```bash
# All commands can be run from anywhere

# RIC container logs
docker logs db
docker logs e2term
docker logs e2mgr
docker logs e2rtmansim

# xApp logs
docker logs sample-xapp-24
docker exec sample-xapp-24 cat /home/container.log

# AI Relay Server logs (from project root)
tail -f relay_server.log

# NS-3 logs (if running in background, from project root)
tail -f ns-3-mmwave-oran/ns3_simulation.log
```

### Test Connections

```bash
# Test AI Relay Server
curl http://localhost:5002/status

# Test xApp connection (from inside xApp container)
docker exec sample-xapp-24 curl http://host.docker.internal:5000/health

# Test Grafana (if dashboard is running)
curl http://localhost:3000/api/health

# Test InfluxDB (if dashboard is running)
curl http://localhost:8086/health
```

---

## Troubleshooting

### RIC Containers Not Starting

```bash
# Check Docker is running
docker info

# Check if ports are in use
lsof -i :3801 -i :3802 -i :3803 -i :6379

# Restart containers (from project root)
cd colosseum-near-rt-ric/setup-scripts
./setup-ric-bronze.sh
```

### AI Relay Server Not Starting

```bash
# Check if ports 5000/5002 are in use
lsof -i :5000 -i :5002

# Kill existing processes (from project root)
./stop_relay.sh

# Check Python dependencies
python3 -c "import flask, requests"

# View error logs (from project root)
tail -f relay_server.log
```

### xApp Not Connecting

```bash
# Check xApp container is running
docker ps --filter "name=sample-xapp-24"

# Check xApp process inside container
docker exec sample-xapp-24 pgrep -f run_xapp.py

# Restart xApp
docker exec sample-xapp-24 bash -c "cd /home/sample-xapp && ./run_xapp.sh"

# Check xApp logs
docker logs sample-xapp-24
```

### NS-3 Not Running

```bash
# From project root
# Check if ns3 is executable
ls -l ns-3-mmwave-oran/ns3
chmod +x ns-3-mmwave-oran/ns3

# Check if scenario file exists
ls -l ns-3-mmwave-oran/scratch/ourv2.cc

# Check build
cd ns-3-mmwave-oran
./ns3 configure
./ns3 build
```

### Dashboard Not Showing Data

```bash
# From project root
cd ai-dashboard

# Check services are running
docker compose ps

# Check CSV files exist (from project root)
ls -l ../gnb_kpis.csv ../ue_kpis.csv

# Import data manually
source venv/bin/activate
python3 import_csv_data.py

# Verify data
python3 verify_setup.py
```

---

## Stopping the System

### Stop All Components

```bash
# 1. Stop NS-3 simulation
pkill -f "ns3 run"

# 2. Stop AI Relay Server (from project root)
./stop_relay.sh

# 3. Stop xApp
docker stop sample-xapp-24

# 4. Stop RIC containers
docker stop db e2term e2mgr e2rtmansim

# 5. Stop Dashboard (if running, from project root)
cd ai-dashboard
docker compose down
```

### Stop Individual Components

```bash
# Stop AI Relay Server only
./stop_relay.sh

# Stop xApp only
docker stop sample-xapp-24

# Stop RIC containers only
docker stop db e2term e2mgr e2rtmansim

# Stop Dashboard only
cd ai-dashboard
docker compose down
```

### Clean Up (Remove Containers)

```bash
# Remove xApp container
docker stop sample-xapp-24
docker rm sample-xapp-24

# Remove RIC containers
docker stop db e2term e2mgr e2rtmansim
docker rm db e2term e2mgr e2rtmansim

# Remove Dashboard containers
cd ai-dashboard
docker compose down -v  # -v removes volumes too
```

---

## Quick Reference

### Automatic Startup
```bash
./deploy.sh
```

### Manual Startup Order
All commands should be run from the project root directory:

1. Import images: `cd colosseum-near-rt-ric/setup-scripts && ./import-wines-images.sh`
2. Setup RIC: `cd colosseum-near-rt-ric/setup-scripts && ./setup-ric-bronze.sh`
3. Start relay: `./run_ai_relay.sh`
4. Setup xApp: `cd colosseum-near-rt-ric/setup-scripts && ./setup-sample-xapp.sh ns-o-ran`
5. Start xApp: `docker exec -d sample-xapp-24 bash -c "cd /home/sample-xapp && ./run_xapp.sh"`
6. Run NS-3: `cd ns-3-mmwave-oran && ./ns3 run scratch/ourv2.cc`

### Useful Commands
```bash
# View all running containers
docker ps

# View logs
docker logs <container-name>
tail -f relay_server.log

# Check ports
lsof -i :5000 -i :5002

# Enter xApp container
docker exec -it sample-xapp-24 bash
```

---

## Additional Resources

- **Dashboard Documentation**: `ai-dashboard/README.md`
- **Dashboard Quick Start**: `ai-dashboard/Dashboard_START.md`
- **RIC Setup**: `colosseum-near-rt-ric/README.md`
- **NS-3 Documentation**: `ns-3-mmwave-oran/README.md`

---

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Review component-specific logs
3. Verify all prerequisites are met
4. Check that Docker and Python are properly installed


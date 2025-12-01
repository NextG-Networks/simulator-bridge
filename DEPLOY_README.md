# Deployment Script Documentation

## Overview

The `deploy.sh` script automates the setup and startup of both the Colosseum RIC environment and the ns-3 simulation. It handles Docker container management, image imports, and process orchestration with proper error checking and logging.

## Prerequisites

- Docker installed and running
- All required directories and scripts present in the project
- Appropriate permissions to run Docker commands (may require sudo)

## Quick Start

### Basic Usage

```bash
# From the project root
./deploy.sh
```

This will:
1. Import Docker images (if not already present)
2. Set up RIC containers (if not already running)
3. Build and start the xApp container
4. Start the xApp inside the container
5. Run the ns-3 simulation in the foreground

### Running ns-3 in Background

```bash
./deploy.sh --background
```

This runs the ns-3 simulation in the background, allowing you to continue using the terminal.

### Skipping Steps

You can skip individual steps if they're already completed:

```bash
# Skip image import (if images already exist)
./deploy.sh --skip-import

# Skip RIC setup (if containers already running)
./deploy.sh --skip-ric

# Skip xApp setup (if container already running)
./deploy.sh --skip-xapp

# Skip ns-3 simulation
./deploy.sh --skip-ns3

# Combine multiple flags
./deploy.sh --skip-import --skip-ric --background

# Non-interactive mode (useful for automation/CI)
./deploy.sh --non-interactive --background

# Skip relay server (if you're running it separately)
./deploy.sh --skip-relay
```

## Command Line Options

| Option | Description |
|--------|-------------|
| `--skip-import` | Skip Docker image import step |
| `--skip-ric` | Skip RIC container setup |
| `--skip-xapp` | Skip xApp container setup and startup |
| `--skip-ns3` | Skip ns-3 simulation |
| `--skip-relay` | Skip AI Relay Server startup |
| `--background` | Run ns-3 simulation in background |
| `--non-interactive` or `-y` | Run without prompts (uses existing containers/processes if found) |

## What the Script Does

### Environment 1: Colosseum RIC

1. **Import Images** (`import-wines-images.sh`)
   - Pulls base images from Wineslab Docker Hub
   - Tags them appropriately
   - Checks if images already exist to avoid redundant pulls

2. **Setup RIC Containers** (`setup-ric-bronze.sh`)
   - Creates Docker network for RIC
   - Builds/creates containers: db, e2term, e2mgr, e2rtmansim
   - Checks if containers are already running

3. **Start AI Relay Server** (`ai_relay_server.py`)
   - Starts the relay server that bridges xApp and external AI
   - Listens on port 5000 for xApp connections
   - Forwards KPIs to external AI server (default: 127.0.0.1:6000)
   - Runs in background with logs written to `relay_server.log`
   - Checks if already running to avoid duplicates

4. **Start xApp Container** (`setup-sample-xapp.sh`)
   - Builds the sample-xapp Docker image
   - Creates and starts the `sample-xapp-24` container
   - Configures the container with proper network and volumes

5. **Run xApp** (`run_xapp.sh` inside container)
   - Starts the xApp Python process
   - Starts the connector
   - Connects to relay server on port 5000
   - Runs in background inside the container

### Environment 2: ns-3 Simulation

1. **Run Simulation**
   - Changes to `ns-3-mmwave-oran` directory
   - Executes `./ns3 run scratch/our.cc`
   - Can run in foreground or background

## Error Handling

The script includes comprehensive error checking:

- **Pre-flight checks**: Verifies Docker is running, all required files/directories exist
- **Step validation**: Each step checks for success before proceeding
- **Container detection**: Automatically detects existing containers/images to avoid redundant operations
- **Clear error messages**: Color-coded output with specific error information

## Troubleshooting

### Docker Permission Issues

If you encounter permission errors, you may need to:

```bash
# Add your user to the docker group
sudo usermod -aG docker $USER
# Log out and back in for changes to take effect
```

Or run with sudo (though the script should handle this automatically if needed).

### Container Already Running

If containers are already running and you want to restart them:

```bash
# Stop and remove containers manually
docker stop db e2term e2mgr e2rtmansim sample-xapp-24
docker rm db e2term e2mgr e2rtmansim sample-xapp-24

# Then run deploy.sh again
./deploy.sh
```

### Viewing Logs

```bash
# xApp container logs
docker logs -f sample-xapp-24

# xApp application log (inside container)
docker exec sample-xapp-24 cat /home/container.log

# Relay server logs
tail -f relay_server.log

# RIC container logs
docker logs db
docker logs e2term
docker logs e2mgr

# ns-3 logs (if running in background)
tail -f ns-3-mmwave-oran/ns3_simulation.log
```

### Entering Containers

```bash
# Enter xApp container
docker exec -it sample-xapp-24 bash

# Enter RIC containers
docker exec -it e2term bash
docker exec -it e2mgr bash
```

### Managing Relay Server

```bash
# Check if relay server is running
ps aux | grep ai_relay_server.py

# Stop relay server (if started by deploy script)
kill $(cat .relay_server.pid) 2>/dev/null || true

# Start relay server manually
./run_ai_relay.sh

# Or run directly
python3 ai_relay_server.py
```

## Assumptions

The script makes the following assumptions:

1. **Project Structure**: All directories are in their expected locations under `/home/hybrid/proj/simulator-bridge/`
2. **Container Names**: xApp container is named `sample-xapp-24` (based on XAPP_IP=10.0.2.24)
3. **Script Locations**: Setup scripts are in `colosseum-near-rt-ric/setup-scripts/`
4. **Docker Network**: RIC containers use the `ric` Docker network
5. **Relay Server**: AI Relay Server listens on port 5000 and forwards to external AI on port 6000 (configurable via environment variables)
6. **Interactive Mode**: By default, the script may prompt for confirmation when containers already exist

## Converting to Docker Compose

While the current setup uses individual scripts, this could be converted to Docker Compose for better orchestration. A `docker-compose.yml` would provide:

- **Benefits**:
  - Single command to start/stop all services
  - Better dependency management
  - Easier configuration management
  - Built-in health checks
  - Service scaling capabilities

- **Considerations**:
  - The current setup has complex build processes and network configurations
  - Some scripts modify container configurations at runtime
  - Would require refactoring the setup scripts into Docker Compose services

If you'd like, I can create a `docker-compose.yml` file that replicates this setup. However, the shell script approach is more flexible for the current workflow and allows for incremental improvements.

## Manual Steps (for reference)

If you need to run steps manually:

```bash
# Environment 1
cd /proj/simulator-bridge/colosseum-near-rt-ric/setup-scripts
./import-wines-images.sh
./setup-ric-bronze.sh
./start-xapp-ns-o-oran.sh
# Inside container:
cd ../sample-xapp
./run_xapp.sh

# Environment 2 (new terminal)
cd /proj/simulator-bridge/ns-3-mmwave-oran
./ns3 run scratch/our.cc
```


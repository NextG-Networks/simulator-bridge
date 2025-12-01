#!/bin/bash
#
# Automated deployment script for AI Dashboard (Grafana + InfluxDB)
# This script automates the setup of Grafana, InfluxDB, and the CSV bridge
#
# Usage: ./deploy_dashboard.sh [--skip-install] [--skip-setup] [--skip-import] [--skip-bridge] [--background] [--non-interactive|-y]
#

set -e  # Exit on error
set -o pipefail  # Exit on pipe failure

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DASHBOARD_DIR="$SCRIPT_DIR"
CSV_GNB_FILE="${PROJECT_ROOT}/gnb_kpis.csv"
CSV_UE_FILE="${PROJECT_ROOT}/ue_kpis.csv"
BRIDGE_SCRIPT="${DASHBOARD_DIR}/csv_to_influxdb.py"
IMPORT_SCRIPT="${DASHBOARD_DIR}/import_csv_data.py"
BRIDGE_LOG_FILE="${DASHBOARD_DIR}/csv_bridge.log"
BRIDGE_PID_FILE="${DASHBOARD_DIR}/.csv_bridge.pid"

# Flags
SKIP_INSTALL=false
SKIP_SETUP=false
SKIP_IMPORT=false
SKIP_BRIDGE=false
RUN_BACKGROUND=false
NON_INTERACTIVE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-install)
            SKIP_INSTALL=true
            shift
            ;;
        --skip-setup)
            SKIP_SETUP=true
            shift
            ;;
        --skip-import)
            SKIP_IMPORT=true
            shift
            ;;
        --skip-bridge)
            SKIP_BRIDGE=true
            shift
            ;;
        --background)
            RUN_BACKGROUND=true
            shift
            ;;
        --non-interactive|-y)
            NON_INTERACTIVE=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--skip-install] [--skip-setup] [--skip-import] [--skip-bridge] [--background] [--non-interactive|-y]"
            exit 1
            ;;
    esac
done

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command() {
    if ! command -v "$1" &> /dev/null; then
        log_error "$1 is not installed or not in PATH"
        exit 1
    fi
}

check_docker_running() {
    if ! docker info &> /dev/null; then
        log_error "Docker is not running. Please start Docker and try again."
        exit 1
    fi
}

check_file_exists() {
    if [ ! -f "$1" ]; then
        log_error "Required file not found: $1"
        exit 1
    fi
}

check_dir_exists() {
    if [ ! -d "$1" ]; then
        log_error "Required directory not found: $1"
        exit 1
    fi
}

run_step() {
    local step_name="$1"
    shift
    log_info "Running: $step_name"
    if "$@"; then
        log_success "$step_name completed"
    else
        log_error "$step_name failed"
        exit 1
    fi
}

# Activate virtual environment if it exists
activate_venv() {
    if [ -f "${DASHBOARD_DIR}/venv/bin/activate" ]; then
        source "${DASHBOARD_DIR}/venv/bin/activate"
        return 0
    elif [ -n "$VIRTUAL_ENV" ]; then
        # Already in a virtual environment
        return 0
    fi
    return 1
}

# Get Python command (use venv if available)
get_python_cmd() {
    if activate_venv 2>/dev/null; then
        echo "python"
    else
        echo "python3"
    fi
}

is_bridge_running() {
    if [ -f "$BRIDGE_PID_FILE" ]; then
        local pid=$(cat "$BRIDGE_PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            # Check if it's actually the bridge script
            if grep -q "csv_to_influxdb.py" "/proc/$pid/cmdline" 2>/dev/null; then
                return 0
            fi
        fi
        # PID file exists but process is not running, clean up
        rm -f "$BRIDGE_PID_FILE"
    fi
    return 1
}

start_csv_bridge() {
    log_info "=========================================="
    log_info "Starting CSV to InfluxDB Bridge"
    log_info "=========================================="
    
    if is_bridge_running; then
        local pid=$(cat "$BRIDGE_PID_FILE")
        log_warning "CSV bridge is already running (PID: $pid)"
        log_info "To restart, stop it first: kill $pid"
        return 0
    fi
    
    # Activate venv and check if Python dependencies are installed
    PYTHON_CMD=$(get_python_cmd)
    if ! $PYTHON_CMD -c "import influxdb_client, pandas, watchdog" 2>/dev/null; then
        log_error "Python dependencies not installed. Run: ./install_dashboard.sh"
        exit 1
    fi
    
    # Check if CSV files exist
    if [ ! -f "$CSV_GNB_FILE" ] && [ ! -f "$CSV_UE_FILE" ]; then
        log_warning "CSV files not found. Bridge will start but wait for files to appear."
        log_info "Expected files: $CSV_GNB_FILE, $CSV_UE_FILE"
    fi
    
    # Check if InfluxDB is accessible
    if ! curl -s http://localhost:8086/health > /dev/null 2>&1; then
        log_error "InfluxDB is not accessible at http://localhost:8086"
        log_info "Make sure InfluxDB is running: docker compose up -d"
        exit 1
    fi
    
    cd "$DASHBOARD_DIR" || exit 1
    
    # Activate venv for Python commands
    PYTHON_CMD=$(get_python_cmd)
    activate_venv
    
    if [ "$RUN_BACKGROUND" = true ]; then
        log_info "Starting CSV bridge in background..."
        nohup $PYTHON_CMD "$BRIDGE_SCRIPT" > "$BRIDGE_LOG_FILE" 2>&1 &
        local pid=$!
        echo "$pid" > "$BRIDGE_PID_FILE"
        
        # Wait a moment and check if it started successfully
        sleep 2
        if ps -p "$pid" > /dev/null 2>&1; then
            log_success "CSV bridge started in background (PID: $pid)"
            log_info "Logs are being written to: $BRIDGE_LOG_FILE"
            log_info "To view logs: tail -f $BRIDGE_LOG_FILE"
            log_info "To stop: kill $pid"
        else
            log_error "CSV bridge failed to start. Check logs: $BRIDGE_LOG_FILE"
            rm -f "$BRIDGE_PID_FILE"
            exit 1
        fi
    else
        log_info "Starting CSV bridge in foreground..."
        log_info "Press Ctrl+C to stop the bridge"
        run_step "Start CSV bridge" $PYTHON_CMD "$BRIDGE_SCRIPT"
    fi
}

# ============================================================================
# Pre-flight checks
# ============================================================================

log_info "=========================================="
log_info "AI Dashboard Deployment"
log_info "=========================================="
log_info ""

log_info "Performing pre-flight checks..."
check_command docker
check_docker_running
check_command python3
check_dir_exists "$PROJECT_ROOT"
check_dir_exists "$DASHBOARD_DIR"
check_file_exists "${DASHBOARD_DIR}/install_dashboard.sh"
check_file_exists "${DASHBOARD_DIR}/setup_grafana.sh"
check_file_exists "${DASHBOARD_DIR}/docker-compose.yml"

log_success "All pre-flight checks passed"
log_info ""

# ============================================================================
# Step 1: Install Dependencies
# ============================================================================

if [ "$SKIP_INSTALL" = false ]; then
    log_info "=========================================="
    log_info "Step 1: Install Dependencies"
    log_info "=========================================="
    
    log_info "Checking if dependencies are already installed..."
    
    # Check with venv if available
    PYTHON_CMD=$(get_python_cmd)
    activate_venv
    
    if $PYTHON_CMD -c "import influxdb_client, pandas, watchdog" 2>/dev/null; then
        log_warning "Python dependencies appear to be installed. Skipping installation."
        log_info "To reinstall, run: ./install_dashboard.sh"
    else
        cd "$DASHBOARD_DIR" || exit 1
        run_step "Install Python dependencies" ./install_dashboard.sh
        # Re-check after installation
        activate_venv
        PYTHON_CMD=$(get_python_cmd)
    fi
    
    log_info ""
else
    log_warning "Skipping dependency installation (--skip-install flag set)"
fi

# ============================================================================
# Step 2: Setup Grafana and InfluxDB
# ============================================================================

if [ "$SKIP_SETUP" = false ]; then
    log_info "=========================================="
    log_info "Step 2: Setup Grafana and InfluxDB"
    log_info "=========================================="
    
    log_info "Checking if Grafana and InfluxDB are already set up..."
    
    # Check if docker-compose.yml exists and services are running
    if docker ps --format "{{.Names}}" | grep -q "ns3-grafana\|ns3-influxdb" 2>/dev/null; then
        log_warning "Grafana/InfluxDB containers are already running."
        
        if [ "$NON_INTERACTIVE" = false ]; then
            read -p "Do you want to restart them? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                cd "$DASHBOARD_DIR" || exit 1
                log_info "Restarting services..."
                docker compose down
                docker compose up -d
                log_success "Services restarted"
                
                log_info "Waiting for services to be ready..."
                sleep 5
                
                # Check if services are healthy
                max_attempts=30
                attempt=0
                while [ $attempt -lt $max_attempts ]; do
                    if curl -s http://localhost:8086/health > /dev/null 2>&1 && \
                       curl -s http://localhost:3000/api/health > /dev/null 2>&1; then
                        log_success "Services are ready"
                        break
                    fi
                    attempt=$((attempt + 1))
                    sleep 1
                done
                
                if [ $attempt -eq $max_attempts ]; then
                    log_error "Services did not become ready in time"
                    log_info "Check logs: docker compose logs"
                    exit 1
                fi
            else
                log_info "Keeping existing services running"
            fi
        else
            log_info "Non-interactive mode: keeping existing services running"
        fi
    else
        # Check if setup script needs to be run
        if [ ! -f "${DASHBOARD_DIR}/grafana/provisioning/datasources/influxdb.yml" ]; then
            cd "$DASHBOARD_DIR" || exit 1
            run_step "Run Grafana setup script" ./setup_grafana.sh
        fi
        
        cd "$DASHBOARD_DIR" || exit 1
        log_info "Starting Grafana and InfluxDB containers..."
        run_step "Start Docker services" docker compose up -d
        
        log_info "Waiting for services to be ready..."
        sleep 5
        
        # Check if services are healthy
        max_attempts=30
        attempt=0
        while [ $attempt -lt $max_attempts ]; do
            if curl -s http://localhost:8086/health > /dev/null 2>&1 && \
               curl -s http://localhost:3000/api/health > /dev/null 2>&1; then
                log_success "Services are ready"
                break
            fi
            attempt=$((attempt + 1))
            sleep 1
        done
        
        if [ $attempt -eq $max_attempts ]; then
            log_error "Services did not become ready in time"
            log_info "Check logs: docker compose logs"
            exit 1
        fi
    fi
    
    log_info ""
else
    log_warning "Skipping Grafana/InfluxDB setup (--skip-setup flag set)"
fi

# ============================================================================
# Step 3: Import CSV Data
# ============================================================================

if [ "$SKIP_IMPORT" = false ]; then
    log_info "=========================================="
    log_info "Step 3: Import CSV Data"
    log_info "=========================================="
    
    # Check if CSV files exist
    if [ ! -f "$CSV_GNB_FILE" ] && [ ! -f "$CSV_UE_FILE" ]; then
        log_warning "CSV files not found. Skipping import."
        log_info "Expected files: $CSV_GNB_FILE, $CSV_UE_FILE"
        log_info "You can import data later with: python3 import_csv_data.py"
    else
        # Check if InfluxDB is accessible (wait a bit if services just started)
        log_info "Checking InfluxDB accessibility..."
        max_attempts=10
        attempt=0
        while [ $attempt -lt $max_attempts ]; do
            if curl -s http://localhost:8086/health > /dev/null 2>&1; then
                log_success "InfluxDB is accessible"
                break
            fi
            attempt=$((attempt + 1))
            if [ $attempt -lt $max_attempts ]; then
                sleep 1
            fi
        done
        
        if [ $attempt -eq $max_attempts ]; then
            log_error "InfluxDB is not accessible after waiting. Make sure services are running."
            log_info "Check status: docker compose ps"
            log_info "Check logs: docker compose logs influxdb"
            exit 1
        fi
        
        # Activate venv and check if data already exists
        PYTHON_CMD=$(get_python_cmd)
        activate_venv
        
        if $PYTHON_CMD -c "
from influxdb_client import InfluxDBClient
import os
client = InfluxDBClient(url='http://localhost:8086', token='my-super-secret-auth-token', org='ns3-org')
query_api = client.query_api()
result = query_api.query(org='ns3-org', query='from(bucket: \"ns3-kpis\") |> range(start: -30d) |> limit(n: 1)')
count = sum(1 for table in result for _ in table.records)
client.close()
exit(0 if count > 0 else 1)
" 2>/dev/null; then
            if [ "$NON_INTERACTIVE" = false ]; then
                log_warning "Data already exists in InfluxDB."
                read -p "Do you want to re-import? This will add duplicate data. (y/N): " -n 1 -r
                echo
                if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                    log_info "Skipping import"
                else
                    cd "$DASHBOARD_DIR" || exit 1
                    activate_venv
                    PYTHON_CMD=$(get_python_cmd)
                    run_step "Import CSV data" $PYTHON_CMD "$IMPORT_SCRIPT"
                fi
            else
                log_info "Data already exists. Skipping import (non-interactive mode)"
            fi
        else
            cd "$DASHBOARD_DIR" || exit 1
            activate_venv
            PYTHON_CMD=$(get_python_cmd)
            run_step "Import CSV data" $PYTHON_CMD "$IMPORT_SCRIPT"
        fi
    fi
    
    log_info ""
else
    log_warning "Skipping CSV data import (--skip-import flag set)"
fi

# ============================================================================
# Step 4: Start CSV Bridge
# ============================================================================

if [ "$SKIP_BRIDGE" = false ]; then
    start_csv_bridge
    log_info ""
else
    log_warning "Skipping CSV bridge (--skip-bridge flag set)"
fi

# ============================================================================
# Summary
# ============================================================================

log_info "=========================================="
log_info "Deployment Summary"
log_info "=========================================="

log_info "Docker Services Status:"
docker ps --filter "name=ns3-grafana" --filter "name=ns3-influxdb" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" || true

if [ "$SKIP_BRIDGE" = false ]; then
    if is_bridge_running; then
        bridge_pid=$(cat "$BRIDGE_PID_FILE")
        log_info "CSV Bridge: Running (PID: $bridge_pid)"
    else
        log_warning "CSV Bridge: Not running"
    fi
fi

log_success "Deployment completed!"
log_info ""
log_info "Access your services:"
log_info "  Grafana Dashboard:  http://localhost:3000 (admin/admin)"
log_info "  InfluxDB UI:        http://localhost:8086 (admin/admin123456)"
log_info ""
log_info "Quick Links:"
log_info "  📊 Open Grafana:     http://localhost:3000"
log_info "  📈 View Dashboard:   http://localhost:3000/d/ns3-kpis-dashboard"
log_info "  💾 InfluxDB Data:    http://localhost:8086"
log_info ""
log_info "Useful commands:"
log_info "  View Grafana logs:    docker compose logs grafana"
log_info "  View InfluxDB logs:   docker compose logs influxdb"
log_info "  Stop services:        docker compose down"
log_info "  Restart services:     docker compose restart"
if [ "$SKIP_BRIDGE" = false ] && is_bridge_running; then
    log_info "  View bridge logs:      tail -f $BRIDGE_LOG_FILE"
    log_info "  Stop bridge:           kill \$(cat $BRIDGE_PID_FILE) 2>/dev/null || true"
fi
log_info "  Verify setup:         python3 verify_setup.py"
log_info "  Import data:          python3 import_csv_data.py"
log_info ""
log_info "For detailed documentation, see:"
log_info "  START_TUTORIAL.md - Step-by-step guide"
log_info "  README_DASHBOARD.md - Complete documentation"


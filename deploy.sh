#!/bin/bash
#
# Automated deployment script for simulator-bridge environment
# This script automates the setup of both Colosseum RIC environment and ns-3 simulation
#
# Usage: ./deploy.sh [--skip-import] [--skip-ric] [--skip-xapp] [--skip-ns3] [--skip-relay] [--background] [--non-interactive|-y]
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
PROJECT_ROOT="/home/hybrid/proj/simulator-bridge"
SETUP_SCRIPTS_DIR="${PROJECT_ROOT}/colosseum-near-rt-ric/setup-scripts"
SAMPLE_XAPP_DIR="${PROJECT_ROOT}/colosseum-near-rt-ric/setup/sample-xapp"
NS3_DIR="${PROJECT_ROOT}/ns-3-mmwave-oran"
NS3_DEFAULT_SCENARIO="ourv2.cc"   # Default ns-3 scratch scenario
NS3_SCENARIO=""                   # Will be selected at runtime
RELAY_SERVER_SCRIPT="${PROJECT_ROOT}/ai_relay_server.py"
RELAY_LOG_FILE="${PROJECT_ROOT}/relay_server.log"
XAPP_CONTAINER_NAME="sample-xapp-24"
RELAY_PID_FILE="${PROJECT_ROOT}/.relay_server.pid"

# Flags
SKIP_IMPORT=false
SKIP_RIC=false
SKIP_XAPP=false
SKIP_NS3=false
SKIP_RELAY=false
RUN_BACKGROUND=false
NON_INTERACTIVE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-import)
            SKIP_IMPORT=true
            shift
            ;;
        --skip-ric)
            SKIP_RIC=true
            shift
            ;;
        --skip-xapp)
            SKIP_XAPP=true
            shift
            ;;
        --skip-ns3)
            SKIP_NS3=true
            shift
            ;;
        --skip-relay)
            SKIP_RELAY=true
            shift
            ;;
        --scenario)
            NS3_SCENARIO="$2"
            shift 2
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
            echo "Usage: $0 [--skip-import] [--skip-ric] [--skip-xapp] [--skip-ns3] [--skip-relay] [--scenario <file.cc>] [--background] [--non-interactive|-y]"
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

wait_for_container_running() {
    local name="$1"
    local timeout="${2:-30}"  # seconds
    local elapsed=0

    while [ "$elapsed" -lt "$timeout" ]; do
        local state
        state=$(docker inspect -f '{{.State.Running}}' "$name" 2>/dev/null || echo "false")
        if [ "$state" = "true" ]; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    return 1
}

select_ns3_scenario() {
    local scratch_dir="${NS3_DIR}/scratch"
    local scenarios=()

    # Collect all .cc files in scratch
    mapfile -t scenarios < <(cd "$scratch_dir" && ls *.cc 2>/dev/null | sort)

    if [ ${#scenarios[@]} -eq 0 ]; then
        log_error "No .cc scenarios found in ${scratch_dir}"
        exit 1
    fi

    log_info "Available ns-3 scratch scenarios:"
    local i=1
    for s in "${scenarios[@]}"; do
        echo "  [$i] $s"
        i=$((i+1))
    done

    # Determine default choice if NS3_DEFAULT_SCENARIO is present
    local default_choice=""
    for idx in "${!scenarios[@]}"; do
        if [ "${scenarios[$idx]}" = "$NS3_DEFAULT_SCENARIO" ]; then
            default_choice=$((idx+1))
            break
        fi
    done

    while true; do
        if [ -n "$default_choice" ]; then
            read -p "Select ns-3 scenario [1-${#scenarios[@]}] (default: ${default_choice} -> ${NS3_DEFAULT_SCENARIO}): " choice
        else
            read -p "Select ns-3 scenario [1-${#scenarios[@]}]: " choice
        fi

        if [ -z "$choice" ] && [ -n "$default_choice" ]; then
            choice=$default_choice
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le ${#scenarios[@]} ]; then
            NS3_SCENARIO="${scenarios[$((choice-1))]}"
            break
        fi

        echo "Invalid choice. Please enter a number between 1 and ${#scenarios[@]}."
    done

    log_info "Selected ns-3 scenario: scratch/${NS3_SCENARIO}"
}

is_relay_running() {
    if [ -f "$RELAY_PID_FILE" ]; then
        local pid=$(cat "$RELAY_PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
            # Check if it's actually the relay server
            if ps -p "$pid" -o cmd= | grep -q "ai_relay_server.py"; then
                return 0
            fi
        fi
        # PID file exists but process is dead, clean it up
        rm -f "$RELAY_PID_FILE"
    fi
    return 1
}

start_relay_server() {
    if is_relay_running; then
        local pid=$(cat "$RELAY_PID_FILE")
        log_warning "Relay server is already running (PID: $pid)"
        if [ "$NON_INTERACTIVE" = false ]; then
            read -p "Do you want to restart it? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                stop_relay_server
            else
                return 0
            fi
        else
            log_info "Non-interactive mode: Using existing relay server"
            return 0
        fi
    fi
    
    log_info "Starting AI Relay Server..."
    
    # Check if ports are already in use and try to kill relay server processes
    local port_conflict=false
    if command -v lsof &> /dev/null; then
        for port in 5000 5002; do
            local pids=$(lsof -ti :$port 2>/dev/null || true)
            if [ -n "$pids" ]; then
                for pid in $pids; do
                    if ps -p "$pid" > /dev/null 2>&1; then
                        local cmd=$(ps -p "$pid" -o cmd= 2>/dev/null || echo "")
                        if echo "$cmd" | grep -q "ai_relay_server.py"; then
                            log_warning "Port $port is in use by relay server (PID: $pid). Stopping it..."
                            stop_relay_server
                            sleep 1
                        else
                            log_warning "Port $port is in use by another process (PID: $pid)"
                            log_info "Command: $cmd"
                            port_conflict=true
                        fi
                    fi
                done
            fi
        done
    fi
    
    # If there's a port conflict with a non-relay process, ask user
    if [ "$port_conflict" = true ]; then
        if [ "$NON_INTERACTIVE" = false ]; then
            read -p "Continue anyway? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                log_error "Aborted. Please free ports 5000/5002 or stop the conflicting process."
                return 1
            fi
        else
            log_warning "Non-interactive mode: Continuing despite port conflict"
        fi
    fi
    
    # Start relay server in background
    cd "$PROJECT_ROOT" || exit 1
    nohup python3 "$RELAY_SERVER_SCRIPT" > "$RELAY_LOG_FILE" 2>&1 &
    local pid=$!
    echo "$pid" > "$RELAY_PID_FILE"
    
    # Wait a moment and check if it started successfully
    sleep 2
    if ps -p "$pid" > /dev/null 2>&1; then
        log_success "Relay server started (PID: $pid)"
        log_info "Logs: tail -f $RELAY_LOG_FILE"
        return 0
    else
        log_error "Relay server failed to start. Check logs: $RELAY_LOG_FILE"
        rm -f "$RELAY_PID_FILE"
        return 1
    fi
}

stop_relay_server() {
    local killed_any=false
    
    # Method 1: Use PID file if it exists
    if [ -f "$RELAY_PID_FILE" ]; then
        local pid=$(cat "$RELAY_PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
            # Check if it's actually the relay server
            if ps -p "$pid" -o cmd= | grep -q "ai_relay_server.py"; then
                log_info "Stopping relay server (PID: $pid) from PID file..."
                kill "$pid" 2>/dev/null || true
                sleep 1
                # Force kill if still running
                if ps -p "$pid" > /dev/null 2>&1; then
                    log_info "Force killing process $pid..."
                    kill -9 "$pid" 2>/dev/null || true
                fi
                log_success "Stopped relay server (PID: $pid)"
                killed_any=true
            else
                log_warning "PID file contains PID $pid, but it's not the relay server. Cleaning up PID file."
            fi
        else
            log_info "PID file exists but process is not running. Cleaning up PID file."
        fi
        rm -f "$RELAY_PID_FILE"
    fi
    
    # Method 2: Find processes by name (most reliable)
    if command -v pgrep &> /dev/null; then
        local pids_by_name=$(pgrep -f "ai_relay_server.py" 2>/dev/null || true)
        if [ -n "$pids_by_name" ]; then
            log_info "Found relay server processes by name: $pids_by_name"
            for pid in $pids_by_name; do
                if ps -p "$pid" > /dev/null 2>&1; then
                    log_info "Killing relay server process (PID: $pid)..."
                    kill "$pid" 2>/dev/null || true
                    sleep 1
                    if ps -p "$pid" > /dev/null 2>&1; then
                        log_info "Force killing process $pid..."
                        kill -9 "$pid" 2>/dev/null || true
                    fi
                    log_success "Stopped relay server (PID: $pid)"
                    killed_any=true
                fi
            done
        fi
    fi
    
    # Method 3: Find and kill processes using ports 5000 or 5002
    if command -v lsof &> /dev/null; then
        # Check each port separately
        for port in 5000 5002; do
            local pids=$(lsof -ti :$port 2>/dev/null || true)
            if [ -n "$pids" ]; then
                log_info "Found processes using port $port: $pids"
                for pid in $pids; do
                    if ps -p "$pid" > /dev/null 2>&1; then
                        local cmd=$(ps -p "$pid" -o cmd= 2>/dev/null || echo "")
                        if echo "$cmd" | grep -q "ai_relay_server.py"; then
                            log_info "Killing relay server process (PID: $pid) using port $port..."
                            kill "$pid" 2>/dev/null || true
                            sleep 1
                            if ps -p "$pid" > /dev/null 2>&1; then
                                log_info "Force killing process $pid..."
                                kill -9 "$pid" 2>/dev/null || true
                            fi
                            log_success "Stopped relay server (PID: $pid)"
                            killed_any=true
                        else
                            log_warning "Process $pid is using port $port but doesn't appear to be the relay server"
                            log_info "Command: $cmd"
                            # In non-interactive mode, don't ask, just log
                            if [ "$NON_INTERACTIVE" = false ]; then
                                read -p "Kill it anyway? (y/N): " -n 1 -r
                                echo
                                if [[ $REPLY =~ ^[Yy]$ ]]; then
                                    kill "$pid" 2>/dev/null || true
                                    sleep 1
                                    if ps -p "$pid" > /dev/null 2>&1; then
                                        kill -9 "$pid" 2>/dev/null || true
                                    fi
                                    log_success "Killed process $pid"
                                    killed_any=true
                                fi
                            else
                                log_info "Non-interactive mode: Skipping non-relay process $pid"
                            fi
                        fi
                    fi
                done
            fi
        done
    elif command -v fuser &> /dev/null; then
        # Alternative using fuser
        log_info "Using fuser to kill processes on ports 5000/5002..."
        fuser -k 5000/tcp 5002/tcp 2>/dev/null || true
        killed_any=true
    fi
    
    if [ "$killed_any" = false ]; then
        log_info "No relay server processes found running."
    fi
}

run_step() {
    local step_name="$1"
    local step_command="$2"
    
    log_info "Starting: $step_name"
    if eval "$step_command"; then
        log_success "Completed: $step_name"
        return 0
    else
        log_error "Failed: $step_name"
        return 1
    fi
}

# Pre-flight checks
log_info "Performing pre-flight checks..."
check_command docker
check_docker_running
check_dir_exists "$PROJECT_ROOT"
check_dir_exists "$SETUP_SCRIPTS_DIR"
check_dir_exists "$SAMPLE_XAPP_DIR"
check_dir_exists "$NS3_DIR"
check_dir_exists "${NS3_DIR}/scratch"
check_file_exists "${SETUP_SCRIPTS_DIR}/import-wines-images.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/setup-ric-bronze.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/start-xapp-ns-o-ran.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/setup-sample-xapp.sh"
check_file_exists "${SAMPLE_XAPP_DIR}/run_xapp.sh"
check_file_exists "${NS3_DIR}/ns3"

# If scenario was provided via --scenario, check that it exists
if [ -n "$NS3_SCENARIO" ]; then
    check_file_exists "${NS3_DIR}/scratch/${NS3_SCENARIO}"
fi

# Check relay server if not skipping
if [ "$SKIP_RELAY" = false ]; then
    check_command python3
    check_file_exists "$RELAY_SERVER_SCRIPT"
fi

log_success "All pre-flight checks passed"

# Change to project root
cd "$PROJECT_ROOT" || exit 1

# ============================================================================
# Environment 1: Colosseum RIC Setup
# ============================================================================

log_info "=========================================="
log_info "Environment 1: Colosseum RIC Setup"
log_info "=========================================="

# Step 1: Import base images
if [ "$SKIP_IMPORT" = false ]; then
    log_info "Checking if Docker images already exist..."
    
    # Check if images are already imported
    if docker image inspect e2term:bronze &> /dev/null && \
       docker image inspect e2mgr:bronze &> /dev/null && \
       docker image inspect e2rtmansim:bronze &> /dev/null && \
       docker image inspect dbaas:bronze &> /dev/null; then
        log_warning "Docker images already exist. Skipping import."
        log_info "To force re-import, run: docker pull wineslab/colo-ran-*:bronze"
    else
        cd "$SETUP_SCRIPTS_DIR" || exit 1
        run_step "Import Wines images" "./import-wines-images.sh"
        cd "$PROJECT_ROOT" || exit 1
    fi
else
    log_warning "Skipping image import (--skip-import flag set)"
fi

# Step 2: Setup RIC containers
if [ "$SKIP_RIC" = false ]; then
    log_info "Checking if RIC containers are already running..."
    
    # Check if containers are running
    if docker ps --format '{{.Names}}' | grep -q "^db$" && \
       docker ps --format '{{.Names}}' | grep -q "^e2term$" && \
       docker ps --format '{{.Names}}' | grep -q "^e2mgr$" && \
       docker ps --format '{{.Names}}' | grep -q "^e2rtmansim$"; then
        log_warning "RIC containers are already running. Skipping setup."
        log_info "To restart, run: cd $SETUP_SCRIPTS_DIR && ./setup-ric-bronze.sh"
    else
        cd "$SETUP_SCRIPTS_DIR" || exit 1
        run_step "Setup RIC Bronze containers" "./setup-ric-bronze.sh"
        cd "$PROJECT_ROOT" || exit 1
        
        # Wait a bit for containers to fully start
        log_info "Waiting for RIC containers to initialize..."
        sleep 5
    fi
else
    log_warning "Skipping RIC setup (--skip-ric flag set)"
fi

# Step 3: Start AI Relay Server (before xApp so it's ready to accept connections)
if [ "$SKIP_RELAY" = false ]; then
    log_info "Starting AI Relay Server..."
    if start_relay_server; then
        log_success "Relay server is ready"
    else
        log_error "Failed to start relay server. xApp may not be able to connect."
        if [ "$NON_INTERACTIVE" = false ]; then
            read -p "Continue anyway? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                exit 1
            fi
        else
            log_warning "Continuing in non-interactive mode, but relay server may not be available"
        fi
    fi
else
    log_warning "Skipping relay server (--skip-relay flag set)"
fi

# Step 4: Start xApp container
if [ "$SKIP_XAPP" = false ]; then
    log_info "Checking if xApp container exists..."
    
    if docker ps --format '{{.Names}}' | grep -q "^${XAPP_CONTAINER_NAME}$"; then
        log_warning "xApp container ${XAPP_CONTAINER_NAME} is already running."
        if [ "$NON_INTERACTIVE" = true ]; then
            log_info "Non-interactive mode: Using existing xApp container"
        else
            read -p "Do you want to restart it? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                log_info "Stopping and removing existing xApp container..."
                docker kill "$XAPP_CONTAINER_NAME" 2>/dev/null || true
                docker rm "$XAPP_CONTAINER_NAME" 2>/dev/null || true
            else
                log_info "Using existing xApp container"
            fi
        fi
    fi
    
    # Check if container needs to be created
    if ! docker ps -a --format '{{.Names}}' | grep -q "^${XAPP_CONTAINER_NAME}$"; then
        cd "$SETUP_SCRIPTS_DIR" || exit 1
        log_info "Building and starting xApp container..."
        
        # Kill and remove old container if it exists (from start-xapp-ns-o-ran.sh)
        docker kill "$XAPP_CONTAINER_NAME" 2>/dev/null || true
        docker rm "$XAPP_CONTAINER_NAME" 2>/dev/null || true
        docker rmi sample-xapp:latest 2>/dev/null || true
        
        run_step "Setup sample xApp container" "./setup-sample-xapp.sh ns-o-ran"
        cd "$PROJECT_ROOT" || exit 1
    fi

    # Ensure container is running before trying to exec into it
    log_info "Ensuring xApp container ${XAPP_CONTAINER_NAME} is running..."
    if ! wait_for_container_running "$XAPP_CONTAINER_NAME" 5; then
        log_warning "xApp container ${XAPP_CONTAINER_NAME} is not running. Attempting to start it..."
        docker start "$XAPP_CONTAINER_NAME" >/dev/null 2>&1 || true

        if ! wait_for_container_running "$XAPP_CONTAINER_NAME" 30; then
            log_error "xApp container ${XAPP_CONTAINER_NAME} is not running after setup/start attempts."
            log_info "Container status:"
            docker ps -a --filter "name=${XAPP_CONTAINER_NAME}" || true
            log_info "Check logs with: docker logs ${XAPP_CONTAINER_NAME}"
            # Do not attempt to start xApp inside a non-running container
            SKIP_XAPP_START=true
        fi
    fi
    
    # Step 5: Start the xApp inside the container
    log_info "Starting xApp inside container..."
    
    # Check if xApp is already running (only if container is running)
    if wait_for_container_running "$XAPP_CONTAINER_NAME" 1 && \
       docker exec "$XAPP_CONTAINER_NAME" pgrep -f "run_xapp.py" &> /dev/null; then
        log_warning "xApp appears to be already running in the container"
        if [ "$NON_INTERACTIVE" = true ]; then
            log_info "Non-interactive mode: Using existing xApp process"
            SKIP_XAPP_START=true
        else
            read -p "Do you want to restart it? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                log_info "Stopping existing xApp processes..."
                docker exec "$XAPP_CONTAINER_NAME" kill -9 $(docker exec "$XAPP_CONTAINER_NAME" pidof python3) 2>/dev/null || true
            else
                log_info "Using existing xApp process"
                SKIP_XAPP_START=true
            fi
        fi
    fi
    
    if [ "${SKIP_XAPP_START:-false}" = false ]; then
        # Start xApp in the background inside the container
        log_info "Executing run_xapp.sh inside container..."
        docker exec -d "$XAPP_CONTAINER_NAME" bash -c "cd /home/sample-xapp && ./run_xapp.sh"
        
        # Wait a moment and check if it started successfully
        sleep 2
        if docker exec "$XAPP_CONTAINER_NAME" pgrep -f "run_xapp.py" &> /dev/null; then
            log_success "xApp started successfully in container"
        else
            log_error "xApp may not have started correctly. Check container logs:"
            log_info "  docker logs $XAPP_CONTAINER_NAME"
            log_info "  docker exec $XAPP_CONTAINER_NAME cat /home/container.log"
        fi
    fi
else
    log_warning "Skipping xApp setup (--skip-xapp flag set)"
fi

# ============================================================================
# Environment 2: ns-3 Simulation
# ============================================================================

if [ "$SKIP_NS3" = false ]; then
    log_info "=========================================="
    log_info "Environment 2: ns-3 Simulation"
    log_info "=========================================="
    
    cd "$NS3_DIR" || exit 1
    
    # Check if ns3 is executable
    if [ ! -x "./ns3" ]; then
        log_warning "ns3 script is not executable. Attempting to make it executable..."
        chmod +x ./ns3 || {
            log_error "Failed to make ns3 executable"
            exit 1
        }
    fi

    # Determine ns-3 scenario if not already set
    if [ -z "$NS3_SCENARIO" ]; then
        if [ "$NON_INTERACTIVE" = true ]; then
            # Non-interactive: use default if it exists, otherwise first .cc file
            if [ -n "$NS3_DEFAULT_SCENARIO" ] && [ -f "${NS3_DIR}/scratch/${NS3_DEFAULT_SCENARIO}" ]; then
                NS3_SCENARIO="$NS3_DEFAULT_SCENARIO"
                log_info "Non-interactive: using default ns-3 scenario scratch/${NS3_SCENARIO}"
            else
                local first_scenario
                first_scenario=$(cd "${NS3_DIR}/scratch" && ls *.cc 2>/dev/null | sort | head -n 1)
                if [ -z "$first_scenario" ]; then
                    log_error "No .cc scenarios found in ${NS3_DIR}/scratch"
                    exit 1
                fi
                NS3_SCENARIO="$first_scenario"
                log_info "Non-interactive: using first ns-3 scenario scratch/${NS3_SCENARIO}"
            fi
        else
            # Interactive selection
            select_ns3_scenario
        fi
    else
        # If NS3_SCENARIO was provided, just log it
        log_info "Using ns-3 scenario from arguments: scratch/${NS3_SCENARIO}"
    fi

    log_info "Starting ns-3 simulation: scratch/${NS3_SCENARIO}"

    if [ "$RUN_BACKGROUND" = true ]; then
        log_info "Running ns-3 simulation in background..."
        nohup ./ns3 run "scratch/${NS3_SCENARIO}" > ns3_simulation.log 2>&1 &
        NS3_PID=$!
        log_success "ns-3 simulation started in background (PID: $NS3_PID)"
        log_info "Logs are being written to: $NS3_DIR/ns3_simulation.log"
        log_info "To view logs: tail -f $NS3_DIR/ns3_simulation.log"
        log_info "To stop: kill $NS3_PID"
    else
        log_info "Running ns-3 simulation in foreground..."
        log_info "Press Ctrl+C to stop the simulation"
        run_step "Run ns-3 simulation" "./ns3 run scratch/${NS3_SCENARIO}"
    fi
else
    log_warning "Skipping ns-3 simulation (--skip-ns3 flag set)"
fi

# ============================================================================
# Summary
# ============================================================================

log_info "=========================================="
log_info "Deployment Summary"
log_info "=========================================="

log_info "RIC Containers Status:"
docker ps --filter "name=db" --filter "name=e2term" --filter "name=e2mgr" --filter "name=e2rtmansim" --format "table {{.Names}}\t{{.Status}}" || true

log_info "xApp Container Status:"
docker ps --filter "name=$XAPP_CONTAINER_NAME" --format "table {{.Names}}\t{{.Status}}" || true

if [ "$SKIP_RELAY" = false ]; then
    if is_relay_running; then
        local relay_pid=$(cat "$RELAY_PID_FILE")
        log_info "Relay Server: Running (PID: $relay_pid)"
    else
        log_warning "Relay Server: Not running"
    fi
fi

if [ "$SKIP_NS3" = false ] && [ "$RUN_BACKGROUND" = true ]; then
    log_info "ns-3 Simulation: Running in background (PID: $NS3_PID)"
fi

log_success "Deployment completed!"
log_info ""
log_info "Useful commands:"
log_info "  View xApp logs: docker logs -f $XAPP_CONTAINER_NAME"
log_info "  View xApp container log: docker exec $XAPP_CONTAINER_NAME cat /home/container.log"
log_info "  Enter xApp container: docker exec -it $XAPP_CONTAINER_NAME bash"
log_info "  View RIC container logs: docker logs <container-name>"
if [ "$SKIP_RELAY" = false ]; then
    log_info "  View relay server logs: tail -f $RELAY_LOG_FILE"
    log_info "  Stop relay server: kill \$(cat $RELAY_PID_FILE) 2>/dev/null || true"
fi
if [ "$SKIP_NS3" = false ] && [ "$RUN_BACKGROUND" = true ]; then
    log_info "  View ns-3 logs: tail -f $NS3_DIR/ns3_simulation.log"
fi


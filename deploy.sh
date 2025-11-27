#!/bin/bash
#
# Automated deployment script for simulator-bridge environment
# This script automates the setup of both Colosseum RIC environment and ns-3 simulation
#
# Usage: ./deploy.sh [--skip-import] [--skip-ric] [--skip-xapp] [--skip-ns3] [--background] [--non-interactive|-y]
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
XAPP_CONTAINER_NAME="sample-xapp-24"

# Flags
SKIP_IMPORT=false
SKIP_RIC=false
SKIP_XAPP=false
SKIP_NS3=false
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
            echo "Usage: $0 [--skip-import] [--skip-ric] [--skip-xapp] [--skip-ns3] [--background] [--non-interactive|-y]"
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
check_file_exists "${SETUP_SCRIPTS_DIR}/import-wines-images.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/setup-ric-bronze.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/start-xapp-ns-o-ran.sh"
check_file_exists "${SETUP_SCRIPTS_DIR}/setup-sample-xapp.sh"
check_file_exists "${SAMPLE_XAPP_DIR}/run_xapp.sh"
check_file_exists "${NS3_DIR}/ns3"
check_file_exists "${NS3_DIR}/scratch/our.cc"

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

# Step 3: Start xApp container
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
        
        # Wait for container to be ready
        log_info "Waiting for xApp container to be ready..."
        sleep 3
    fi
    
    # Step 4: Start the xApp inside the container
    log_info "Starting xApp inside container..."
    
    # Check if xApp is already running
    if docker exec "$XAPP_CONTAINER_NAME" pgrep -f "run_xapp.py" &> /dev/null; then
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
    
    log_info "Starting ns-3 simulation: scratch/our.cc"
    
    if [ "$RUN_BACKGROUND" = true ]; then
        log_info "Running ns-3 simulation in background..."
        nohup ./ns3 run scratch/our.cc > ns3_simulation.log 2>&1 &
        NS3_PID=$!
        log_success "ns-3 simulation started in background (PID: $NS3_PID)"
        log_info "Logs are being written to: $NS3_DIR/ns3_simulation.log"
        log_info "To view logs: tail -f $NS3_DIR/ns3_simulation.log"
        log_info "To stop: kill $NS3_PID"
    else
        log_info "Running ns-3 simulation in foreground..."
        log_info "Press Ctrl+C to stop the simulation"
        run_step "Run ns-3 simulation" "./ns3 run scratch/our.cc"
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
if [ "$SKIP_NS3" = false ] && [ "$RUN_BACKGROUND" = true ]; then
    log_info "  View ns-3 logs: tail -f $NS3_DIR/ns3_simulation.log"
fi


#!/bin/bash
# Run the AI Relay Server

# Dynamically determine script directory and change to it
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

echo "Starting AI Relay Server..."
echo ""
echo "Configuration:"
echo "  - Listening for xApp on: 0.0.0.0:5000"
echo "  - Forwarding to external AI: ${EXTERNAL_AI_HOST:-127.0.0.1}:${EXTERNAL_AI_PORT:-6000}"
echo ""
echo "Environment variables:"
echo "  EXTERNAL_AI_HOST - External AI server host (default: 127.0.0.1)"
echo "  EXTERNAL_AI_PORT - External AI server port (default: 6000)"
echo ""

python3 ai_relay_server.py


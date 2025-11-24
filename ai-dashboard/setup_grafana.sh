#!/bin/bash
# Setup Grafana with InfluxDB for NS3 KPI visualization

set -e

echo "=========================================="
echo "Grafana + InfluxDB Setup for NS3 KPIs"
echo "=========================================="
echo ""

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

# Check for docker compose (plugin) or docker-compose (standalone)
DOCKER_COMPOSE_CMD=""
if docker compose version &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker compose"
    echo "✅ Found docker compose (plugin)"
elif command -v docker-compose &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker-compose"
    echo "✅ Found docker-compose (standalone)"
else
    echo "Error: docker compose not found"
    echo "Please install Docker Compose (it's usually included with Docker Desktop)"
    echo "Or install standalone: https://docs.docker.com/compose/install/"
    exit 1
fi

echo "Creating docker-compose.yml for Grafana + InfluxDB..."
cat > docker-compose.yml << 'EOF'
version: '3.8'

services:
  influxdb:
    image: influxdb:2.7
    container_name: ns3-influxdb
    ports:
      - "8086:8086"
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=admin123456
      - DOCKER_INFLUXDB_INIT_ORG=ns3-org
      - DOCKER_INFLUXDB_INIT_BUCKET=ns3-kpis
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=my-super-secret-auth-token
    volumes:
      - influxdb-data:/var/lib/influxdb2
    networks:
      - ns3-monitoring

  grafana:
    image: grafana/grafana:latest
    container_name: ns3-grafana
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_USER=admin
      - GF_SECURITY_ADMIN_PASSWORD=admin
      - GF_INSTALL_PLUGINS=
    volumes:
      - grafana-data:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
      - ./grafana/dashboards:/var/lib/grafana/dashboards
    depends_on:
      - influxdb
    networks:
      - ns3-monitoring

volumes:
  influxdb-data:
  grafana-data:

networks:
  ns3-monitoring:
    driver: bridge
EOF

echo "✅ Created docker-compose.yml"
echo ""

# Create Grafana provisioning directories
mkdir -p grafana/provisioning/datasources
mkdir -p grafana/provisioning/dashboards
mkdir -p grafana/dashboards

echo "Creating InfluxDB datasource configuration..."
cat > grafana/provisioning/datasources/influxdb.yml << 'EOF'
apiVersion: 1

datasources:
  - name: InfluxDB
    type: influxdb
    access: proxy
    url: http://influxdb:8086
    isDefault: true
    jsonData:
      version: Flux
      organization: ns3-org
      defaultBucket: ns3-kpis
      tlsSkipVerify: true
    secureJsonData:
      token: my-super-secret-auth-token
EOF

echo "✅ Created InfluxDB datasource configuration"
echo ""

echo "Creating dashboard provisioning..."
cat > grafana/provisioning/dashboards/default.yml << 'EOF'
apiVersion: 1

providers:
  - name: 'NS3 Dashboards'
    orgId: 1
    folder: ''
    type: file
    disableDeletion: false
    updateIntervalSeconds: 10
    allowUiUpdates: true
    options:
      path: /var/lib/grafana/dashboards
EOF

echo "✅ Created dashboard provisioning"
echo ""

echo "Creating sample dashboard JSON..."
cat > grafana/dashboards/ns3-kpis.json << 'EOFDASH'
{
  "dashboard": {
    "title": "NS3 KPI Dashboard",
    "tags": ["ns3", "kpi"],
    "timezone": "browser",
    "panels": [
      {
        "id": 1,
        "title": "gNB KPIs",
        "type": "timeseries",
        "targets": [
          {
            "query": "from(bucket: \"ns3-kpis\") |> range(start: -1h) |> filter(fn: (r) => r[\"_measurement\"] == \"gnb_kpis\")",
            "refId": "A"
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 0, "y": 0}
      },
      {
        "id": 2,
        "title": "UE KPIs",
        "type": "timeseries",
        "targets": [
          {
            "query": "from(bucket: \"ns3-kpis\") |> range(start: -1h) |> filter(fn: (r) => r[\"_measurement\"] == \"ue_kpis\")",
            "refId": "A"
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 12, "y": 0}
      }
    ],
    "refresh": "5s",
    "schemaVersion": 27,
    "version": 1
  }
}
EOFDASH

echo "✅ Created sample dashboard"
echo ""

echo "=========================================="
echo "Setup complete!"
echo "=========================================="
echo ""
echo "To start Grafana and InfluxDB:"
echo "  $DOCKER_COMPOSE_CMD up -d"
echo ""
echo "Access Grafana at: http://localhost:3000"
echo "  Username: admin"
echo "  Password: admin"
echo ""
echo "Access InfluxDB at: http://localhost:8086"
echo "  Username: admin"
echo "  Password: admin123456"
echo ""
echo "To stop:"
echo "  $DOCKER_COMPOSE_CMD down"
echo ""
echo "To view logs:"
echo "  $DOCKER_COMPOSE_CMD logs -f"
echo ""


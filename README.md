# Simulator Bridge – Full Deployment Guide

This repository contains the tools and scripts required to deploy the **Simulator Bridge**, including **ns-3 mmWave ORAN**, **E2SIM**, the **AI Dashboard**, and the **Hybrid AI service**.

---

## Repositories

- Simulator Bridge https://github.com/NextG-Networks/simulator-bridge
- Hybrid AI
  https://github.com/NextG-Networks/hybridAI-nextG

---

## Prerequisites

Ensure the following are installed before continuing:

- Linux (Ubuntu recommended, we used version 24.04 LTE)
- `git`
- `docker` and `docker-compose`
- `cmake`, `make`, `gcc/g++`
- `sudo` privileges

---

## Quick Start

Clone the main repository:

```bash
git clone https://github.com/NextG-Networks/simulator-bridge.git
cd simulator-bridge
```

---

## Build E2SIM (Initial Setup)

```bash
cd oran-e2sim/e2sim
mkdir -p build
./build_e2sim.sh 3
sudo ldconfig
cd ../../
```

---

## Configure and Build ns-3-mmwave-oran

### Configure ns-3

```bash
cd ns-3-mmwave-oran
./ns3 configure
```

### Build E2SIM (Required Again)

```bash
cd e2sim
mkdir -p build
./build_e2sim.sh 3
sudo ldconfig
cd ../../
```

---

## Deploy Simulator Bridge

From the root of the `simulator-bridge` repository, run:

```bash
./deploy.sh
```

> **Important**
> All deploy scripts should be run in **separate terminal windows** if multiple services are started.

---

## Deploy AI Dashboard

```bash
cd ai-dashboard
./deploy_dashboard.sh
```

---

## Deploy Hybrid AI (Separate Repository)

Clone the AI repository into a **separate directory**:

```bash
git clone https://github.com/NextG-Networks/hybridAI-nextG.git
cd hybridAI-nextG
```

Deploy the AI service:

```bash
./deploy_ai.sh
```

---

## System Status

Once all steps complete successfully, the following components will be running:

- Simulator Bridge
- ns-3 ORAN integration
- AI Dashboard
- Hybrid AI service

The system is now **fully operational**.

---

## Troubleshooting

- If shared libraries are not found:

  ```bash
  sudo ldconfig
  ```
- Ensure Docker services are running:

  ```bash
  docker ps
  ```
- If scripts are not executable:

  ```bash
  chmod +x *.sh
  ```

---

## License

This project is released for **research and academic use**.

Commercial use is not permitted without prior written consent from the authors.
Please contact the maintainers for licensing inquiries.

---

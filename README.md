# Installation instruction for HybridAI



For the setup you will be needing some System prerequisites before starting the installation of the software


## * **System prerequisites**

Start with checking and installing the essentials.

```
sudo apt-get update
```

Core toolchain and some helpers

```
sudo apt-get install -y build-essential git cmake ninja-build pkg-config \
  autoconf automake libtool bison flex
```

Docker Engine and SCTP/Boost

```
sudo apt-get install -y docker.io libsctp-dev libboost-all-dev
```

NS3 nessecairly

```
sudo apt-get install -y g++ python3 python3-dev \
  libeigen3-dev libgsl-dev libxml2-dev libsqlite3-dev \
  libgtk-3-dev libharfbuzz-dev
```

Enable docker for your user

```
sudo usermod -aG docker "$USER"
newgrp docker
```

Check for seeing of the docker CLI can talk with the system daemon

```
docker context use default
unset DOCKER_HOST
docker ps   
```

## * Cloning the repo SKA ÄNDRAS TILL MAIN SEN!!!!

```
git clone -b xAppCustomCtrMessage https://github.com/NextG-Networks/simulator-bridge.git

```

## * Build and install the E2SIM

```
cd oran-e2sim/e2sim
mkdir -p build
./build_e2sim.sh 3   
sudo ldconfig
cd ../../
```

## * Start the RIC Bronze and XApp


```
cd colosseum-near-rt-ric/setup-scripts

# Import/tag images (first time only)
./import-wines-images.sh

# Start RIC (bronze)
./setup-ric-bronze.sh

# Build xApp image, create container, configure IDs, compile inside, restart
./start-xapp-ns-o-ran.sh

# Run the xApp 
./run_xapp.sh
```

## * Build ns-3 with ORAN

```
  cd ../../ns-3-mmwave-oran

# Clean in case of a previous partial configure
./ns3 clean

# Configure: examples+tests ON (turn off tests if you want faster build)
./ns3 configure --disable-examples --enable-tests

# If you only want minimal build:  ./ns3 configure --enable-examples --disable-tests

# Build
./ns3 build

```

## * Running a scenario

```
./ns3 run scratch/scenario-zero.cc
```

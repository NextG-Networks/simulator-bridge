# Hybrid-AI

This project is for simulating a userequipment inside a network and change the internet configurations with the help of an AI.

The project is built upon the https://openrangym.com/tutorials/ns-o-ran



## Content 
- [Background](#background)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Running](#Running)
- [Configuration](#Configuration)
- [Felsökning](#felsökning)
- [Licens](#licens)

## Background
The background for this project is to see make a more off hands solution on the current problem with an actual person needing to change the internet settings accordingly to what is needed for the userequipments moving inside the network. We want to achive a autonom solution where an AI solves this and in realtime solves the configuration in the network.

## Prerequisites
- OS: Ubuntu 24.04 LTS
- Externa tjänster / portar
- Docker 28.5.1

## Installation
If you get lost you could always check the original installations guide at https://openrangym.com/tutorials/ns-o-ran

Firstly create a folder where you  open bash 
```bash
git clone -b ns-o-ran https://github.com/wineslab/colosseum-near-rt-ric

cd colosseum-near-rt-ric/setup-scripts

```
After this we want to import an image for docker which is the Wines one and then run the setup.
```bash 
./import-wines-images.sh

./setup-ric-bronze.sh
```

We will need three different terminals for running the project. Open one more terminal.

In the first terminal run the following.
```bash 
docker logs e2term -f --since=1s 2>&1 | grep gnb:  
```

In the second one change directory to the following and the run the the file x-app container.

```bash
cd colosseum-near-rt-ric/setup-scripts
./start-xapp-ns-o-ran.sh
```

The second terminal will then enter a shell in which we will want to run this command in.
```bash 
cd /home/sample-xapp
./run_xapp.sh
```
After this the setup for the Near-RT RIC is done.

Now we want to set up the ns-O-RAN
We will need to install and check that we have the right versions for e2sim and ns-3.

```bash
sudo apt-get update
# Requirements for e2sim
sudo apt-get install -y build-essential git cmake libsctp-dev autoconf automake libtool bison flex libboost-all-dev 
# Requirements for ns-3
sudo apt-get install g++ python3
```

After that we want to clone and install the software for e2sim and then build it.

```bash
git clone https://github.com/wineslab/ns-o-ran-e2-sim oran-e2sim # this will create a folder called oran-e2sim
cd oran-e2sim/e2sim/
mkdir build
./build_e2sim.sh 3
```

________________________________________________________HÄR ÄNDRA DE FILERNA PERHAPS???

Now we can open a new Terminal and clone the ns-3-mmwave-oran repository.1

```bash
git clone https://github.com/wineslab/ns-o-ran-ns3-mmwave ns-3-mmwave-oran
cd ns-3-mmwave-oran
```

We then can configure the ns-3

```Bash
./ns3 configure --enable-examples --enable-tests
```

Then we can build  the project now we can run the following
```bash
./ns3 build
```

Lastly for running the basic scenario for the project is the following
```bash
./ns3 run scratch/scenario-zero.cc
```




#TODO ADD API Documentation
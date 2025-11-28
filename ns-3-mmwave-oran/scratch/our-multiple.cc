/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/mmwave-helper.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/mmwave-ue-net-device.h"
#include "ns3/mmwave-enb-net-device.h"
#include "ns3/v4ping-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/netanim-module.h"
#include "ns3/buildings-helper.h"
#include "ns3/mmwave-component-carrier-enb.h"
#include "ns3/mmwave-flex-tti-mac-scheduler.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <vector>

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_3gNB_5UE_MultipleEvents");

// ---------------- Runtime flags ----------------
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(100.0), MakeDoubleChecker<double>(1.0, 3600.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

// ---------------- Global State/Sampling ----------------
struct GlobalState {
  double lastT = 0.0;
  std::vector<uint64_t> lastBytes; // Per UE
  std::vector<double> ewma;        // Per UE
} gS;

AnimationInterface *g_anim = nullptr;

static void SampleAll(const NodeContainer &ueNodes,
                      const NodeContainer &gnbNodes,
                      const ApplicationContainer &sinkApps,
                      double periodSec)
{
  static std::ofstream f;
  static bool headerDone = false;
  uint32_t nUes = ueNodes.GetN();

  if (!headerDone) {
    gS.lastBytes.resize(nUes, 0);
    gS.ewma.resize(nUes, 0.0);

    f.open("sim_timeseries_multiple.csv", std::ios::out | std::ios::trunc);
    f << std::fixed << std::setprecision(6);
    f << "time_s";
    for (uint32_t i = 0; i < nUes; ++i) {
      f << ",ue" << i << "_imsi"
        << ",ue" << i << "_x"
        << ",ue" << i << "_y"
        << ",ue" << i << "_throughput_mbps"
        << ",ue" << i << "_ewma_mbps";
    }
    f << "\n";
    headerDone = true;
  }

  const double now = Simulator::Now().GetSeconds();
  f << now;

  for (uint32_t i = 0; i < nUes; ++i) {
    Ptr<Node> ue = ueNodes.Get(i);
    Vector p = ue->GetObject<MobilityModel>()->GetPosition();
    uint64_t imsi = ue->GetDevice(0)->GetObject<MmWaveUeNetDevice>()->GetImsi();

    // Throughput calc
    double mbps = 0.0;
    if (i < sinkApps.GetN()) {
      Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
      if (sink) {
        uint64_t bytes = sink->GetTotalRx();
        double dt = now - gS.lastT;
        if (gS.lastT > 0.0 && dt > 0.0) {
          mbps = 8.0 * (bytes - gS.lastBytes[i]) / dt / 1e6;
        }
        gS.lastBytes[i] = bytes;
      }
    }

    // EWMA
    const double tau = 1.0; 
    const double alpha = 1.0 - std::exp(-(periodSec / tau));
    gS.ewma[i] = alpha * mbps + (1.0 - alpha) * gS.ewma[i];

    f << "," << imsi
      << "," << p.x
      << "," << p.y
      << "," << mbps
      << "," << gS.ewma[i];
      
    // Update NetAnim
    if (g_anim) {
        std::ostringstream oss;
        oss << "UE" << i << " (" << std::fixed << std::setprecision(1) << mbps << " Mbps)";
        g_anim->UpdateNodeDescription(ue, oss.str());
    }
  }
  f << "\n";
  f.flush();

  gS.lastT = now;

  Simulator::Schedule(Seconds(periodSec), &SampleAll,
                      ueNodes, gnbNodes, sinkApps, periodSec);
}

// ---------------- EVENTS ----------------

// 1. Random Blockage (Same as v3)
static void RandomBlockageEvent(NodeContainer ues)
{
    if (ues.GetN() == 0) return;
    uint32_t ueIdx = rand() % ues.GetN();
    Ptr<Node> ue = ues.Get(ueIdx);
    
    Ptr<mmwave::MmWaveUeNetDevice> ueDev = ue->GetDevice(0)->GetObject<mmwave::MmWaveUeNetDevice>();
    if (ueDev) {
        Ptr<mmwave::MmWaveUePhy> phy = ueDev->GetPhy();
        if (phy) {
            double originalNf = phy->GetNoiseFigure();
            double blockageNf = originalNf + 30.0; 
            
            phy->SetNoiseFigure(blockageNf);
            NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Random Blockage triggered for UE " << ueIdx << " (NF+30dB)");
            
            Simulator::Schedule(Seconds(5.0), [phy, originalNf, ueIdx]() {
                phy->SetNoiseFigure(originalNf);
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Random Blockage ended for UE " << ueIdx);
            });
        }
    }
    double nextTime = 15.0 + (rand() % 15);
    Simulator::Schedule(Seconds(nextTime), &RandomBlockageEvent, ues);
}

// 2. Traffic Spike (Selects one of the OnOffApps on the Remote Host)
static void TrafficSpikeEvent(NodeContainer remoteHosts)
{
    // Assuming 1 RH with multiple apps
    if (remoteHosts.GetN() == 0) return;
    Ptr<Node> rh = remoteHosts.Get(0);
    if (rh->GetNApplications() == 0) return;

    uint32_t appIdx = rand() % rh->GetNApplications();
    Ptr<OnOffApplication> onOffApp = DynamicCast<OnOffApplication>(rh->GetApplication(appIdx));
    
    if (onOffApp) {
        DataRate originalRate("50Mbps");
        DataRate spikeRate("500Mbps");
        
        onOffApp->SetAttribute("DataRate", DataRateValue(spikeRate));
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Traffic Spike triggered for App " << appIdx << " (500Mbps)");
        
        Simulator::Schedule(Seconds(5.0), [onOffApp, originalRate, appIdx]() {
            onOffApp->SetAttribute("DataRate", DataRateValue(originalRate));
            NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Traffic Spike ended for App " << appIdx);
        });
    }
    
    double nextTime = 20.0 + (rand() % 20);
    Simulator::Schedule(Seconds(nextTime), &TrafficSpikeEvent, remoteHosts);
}

// 3. Neighbor Interference (Boosts TxPower of a random gNB)
static void NeighborInterferenceEvent(NodeContainer gnbs)
{
    if (gnbs.GetN() == 0) return;
    uint32_t gnbIdx = rand() % gnbs.GetN();
    Ptr<Node> gnb = gnbs.Get(gnbIdx);

    Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnb->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
    if (enbDev) {
        Ptr<mmwave::MmWaveEnbPhy> phy = enbDev->GetPhy();
        if (phy) {
            double originalPower = phy->GetTxPower();
            double interferencePower = originalPower + 10.0; // +10dBm
            
            phy->SetTxPower(interferencePower);
            NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Neighbor Interference triggered for gNB " << gnbIdx << " (TxPower+10dB)");
            
            Simulator::Schedule(Seconds(5.0), [phy, originalPower, gnbIdx]() {
                phy->SetTxPower(originalPower);
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Event] Neighbor Interference ended for gNB " << gnbIdx);
            });
        }
    }

    double nextTime = 25.0 + (rand() % 25);
    Simulator::Schedule(Seconds(nextTime), &NeighborInterferenceEvent, gnbs);
}


// ---------------- Main ----------------
int main (int argc, char** argv)
{
  CommandLine cmd;
  int rngSeed = 0;
  cmd.AddValue("rngSeed", "Seed (0=random)", rngSeed);
  cmd.Parse(argc, argv);

  if (rngSeed == 0) srand(time(NULL));
  else srand(rngSeed);

  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simTime = simV.Get();
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();

  // RF defaults
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(100e6)); // Wider BW for multiple UEs
  Config::SetDefault("ns3::MmWaveEnbPhy::TxPower",          DoubleValue(30.0));
  Config::SetDefault("ns3::MmWaveUePhy::NoiseFigure",       DoubleValue(7.0));

  fs::create_directories(outDir);
  fs::current_path(outDir);

  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  mmw->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();

  // Create Nodes
  NodeContainer gnbs; gnbs.Create(3);
  NodeContainer ues;  ues.Create(5);
  NodeContainer rh;   rh.Create(1);

  InternetStackHelper ip; 
  ip.Install(ues); 
  ip.Install(rh);

  // gNB Mobility (Fixed)
  {
    MobilityHelper m;
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(25, 25, 10)); // gNB 0
    pos->Add(Vector(25, 75, 10)); // gNB 1
    pos->Add(Vector(75, 50, 10)); // gNB 2
    m.SetPositionAllocator(pos);
    m.Install(gnbs);
  }

  // UE Mobility (Random Walk in box [0,100]x[0,100])
  {
    MobilityHelper m;
    m.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                           "X", StringValue("50.0"),
                           "Y", StringValue("50.0"),
                           "Rho", StringValue("ns3::UniformRandomVariable[Min=0|Max=40]"));
    m.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                       "Bounds", RectangleValue(Rectangle(0, 100, 0, 100)),
                       "Speed", StringValue("ns3::ConstantRandomVariable[Constant=1.5]"),
                       "Mode", StringValue("Time"),
                       "Time", StringValue("2s"));
    m.Install(ues);
  }

  // Core Mobility
  {
    Ptr<Node> sgw = NodeList::GetNode(gnbs.GetN() + ues.GetN()); // Rough guess, better to use ID
    // Actually EPC helper creates SGW/MME hidden nodes.
    // Just set RH position for visualization
    MobilityHelper m;
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> p = CreateObject<ListPositionAllocator>();
    p->Add(Vector(0, 0, 0));
    m.SetPositionAllocator(p);
    m.Install(rh);
  }

  BuildingsHelper::Install(gnbs);
  BuildingsHelper::Install(ues);

  // Install mmWave Devices
  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnbs);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ues);

  // Assign IP
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  
  // Static Routing for UEs
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ues.GetN(); ++u) {
    Ptr<Ipv4StaticRouting> r = srt.GetStaticRouting(ues.Get(u)->GetObject<Ipv4>());
    r->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  }

  // Attach UEs to closest gNB
  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  // Internet Link (PGW <-> RH)
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer internetDevs = p2p.Install(pgw, rh.Get(0));
  
  Ipv4AddressHelper ipv4h; 
  ipv4h.SetBase("10.0.0.0","255.0.0.0");
  Ipv4InterfaceContainer internetIp = ipv4h.Assign(internetDevs);

  // Route from RH to UEs
  Ipv4StaticRoutingHelper srh;
  Ptr<Ipv4StaticRouting> rhRoute = srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>());
  rhRoute->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // Applications
  ApplicationContainer sinkApps;
  const uint16_t portBase = 4000;

  for (uint32_t i=0; i<ues.GetN(); ++i) {
      uint16_t port = portBase + i;
      PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
      sinkApps.Add(sink.Install(ues.Get(i)));

      OnOffHelper client("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(i), port));
      client.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
      client.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
      client.SetAttribute("DataRate", StringValue("50Mbps"));
      client.SetAttribute("PacketSize", UintegerValue(1200));
      client.Install(rh.Get(0)).Start(Seconds(0.5 + i*0.1));
  }
  sinkApps.Start(Seconds(0.0));

  mmw->EnableTraces();

  // NetAnim
  AnimationInterface anim("NetAnimFile_multiple.xml");
  g_anim = &anim;
  anim.SetMobilityPollInterval(Seconds(0.5));
  anim.SkipPacketTracing();
  
  // Set gNB descriptions
  for(uint32_t i=0; i<gnbs.GetN(); ++i) {
      anim.UpdateNodeDescription(gnbs.Get(i), "gNB " + std::to_string(i));
      anim.UpdateNodeColor(gnbs.Get(i), 0, 255, 0); // Green
  }

  // Schedule Sampling
  Simulator::Schedule(Seconds(0.1), &SampleAll,
                      ues, gnbs, sinkApps, 0.1);

  // Schedule Events
  Simulator::Schedule(Seconds(10.0), &RandomBlockageEvent, ues);
  Simulator::Schedule(Seconds(15.0), &TrafficSpikeEvent, rh);
  Simulator::Schedule(Seconds(20.0), &NeighborInterferenceEvent, gnbs);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

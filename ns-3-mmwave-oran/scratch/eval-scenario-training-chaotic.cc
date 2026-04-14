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
#include "ns3/v4ping.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/netanim-module.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/buildings-module.h"
#include "ns3/buildings-helper.h"
#include "ns3/mmwave-component-carrier-enb.h"
#include "ns3/mmwave-flex-tti-mac-scheduler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("Train_Chaos_1HR");

// --- 1 HOUR RUNTIME (3600 seconds) ---
static GlobalValue g_simTime("simTime", "Simulation time (s)", DoubleValue(3600.0), MakeDoubleChecker<double>(1.0, 36000.0));
static GlobalValue g_outDir("outDir", "Output directory", StringValue("out/logs"), MakeStringChecker());

bool g_enableCsvLogging = true;
static double g_simStopTime = 0.0;

// Extracted exactly from your working code
static GlobalValue g_fastMode ("fastMode", "If true, disables E2 overhead", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_controlFileName ("controlFileName","Path to control file", StringValue(""), MakeStringChecker());
static GlobalValue g_e2nrEnabled ("e2nrEnabled","NR E2 reports", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2du ("e2du","DU reports", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuUp ("e2cuUp","CU-UP reports", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuCp ("e2cuCp","CU-CP reports", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_indicationPeriodicity ("indicationPeriodicity","E2 Periodicity (s)", DoubleValue(0.5), MakeDoubleChecker<double>(0.01, 2.0));
static GlobalValue g_enableE2FileLogging ("enableE2FileLogging","Offline logging", BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_reducedPmValues ("reducedPmValues", "Subset pm containers", BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_e2lteEnabled ("e2lteEnabled","LTE E2 reports", BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2TermIp ("e2TermIp","RIC IP", StringValue("10.0.2.10"), MakeStringChecker());
static GlobalValue q_useSemaphores ("useSemaphores","Semaphores", BooleanValue(false), MakeBooleanChecker());

struct GlobalState { double lastT = 0.0; std::vector<uint64_t> lastBytes; std::vector<double> ewma; bool seenPing = false; double lastPingMs = 0.0; } gS;

static std::string g_activeMcsEvent = "None";
static bool g_activeBlockage = false;
static uint64_t g_blockageImsi = 0;
static bool g_activeTrafficSpike = false;
static uint32_t g_trafficSpikeRhIdx = 0;
AnimationInterface *g_anim = nullptr;

static void PingRttCallback(Time rtt) { gS.lastPingMs = rtt.GetMilliSeconds(); gS.seenPing = true; }

// --- EXACT SAMPLER FROM YOUR WORKING CODE ---
static void SampleAll(const NodeContainer &ueNodes, const NetDeviceContainer &ueDevs, Ptr<Node> gnbNode, double covRadius, ApplicationContainer sinkApps, double periodSec) {
  static std::ofstream f; static bool headerDone = false; static uint32_t sampleCount = 0;
  static Ptr<mmwave::MmWaveFlexTtiMacScheduler> cachedFlexSched = nullptr; static bool schedulerCached = false;
  if (g_enableCsvLogging) {
    if (!headerDone) {
      f.open("sim_timeseries_train.csv", std::ios::out | std::ios::trunc);
      f << std::fixed << std::setprecision(6); f << "time_s";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) f << ",ue" << i << "_imsi,ue" << i << "_x,ue" << i << "_y,ue" << i << "_z,ue" << i << "_dist_to_gnb_m,ue" << i << "_inside";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) f << ",throughput_ue" << i << "_mbps";
      f << ",ping_ms,mcs_dl,mcs_ul,event_mcs_type,event_blockage,event_traffic_spike\n";
      headerDone = true;
    }
    const double now = Simulator::Now().GetSeconds();
    Vector gp = gnbNode->GetObject<MobilityModel>()->GetPosition(); f << now;
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
      Vector p = ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
      double dist = std::sqrt(pow(p.x - gp.x, 2) + pow(p.y - gp.y, 2) + pow(p.z - gp.z, 2));
      f << "," << ueDevs.Get(i)->GetObject<MmWaveUeNetDevice>()->GetImsi() << "," << p.x << "," << p.y << "," << p.z << "," << dist << "," << (dist <= covRadius ? 1 : 0);
    }
    if (gS.lastBytes.size() < sinkApps.GetN()) { gS.lastBytes.resize(sinkApps.GetN(), 0); gS.ewma.resize(sinkApps.GetN(), 0.0); }
    double dt = now - gS.lastT;
    for (uint32_t i = 0; i < sinkApps.GetN(); ++i) {
      double mbps = 0.0; Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
      if (sink) {
        uint64_t bytes = sink->GetTotalRx();
        if (gS.lastT > 0.0 && dt > 0.0) mbps = 8.0 * (bytes - gS.lastBytes[i]) / dt / 1e6;
        gS.lastBytes[i] = bytes; f << "," << mbps;
      } else f << ",0.0";
    }
    gS.lastT = now; double pingMs = gS.seenPing ? gS.lastPingMs : 0.0;
    uint8_t mcsDl = 255; uint8_t mcsUl = 255;
    if (!schedulerCached) {
      Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnbNode->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
      if (enbDev && !enbDev->GetCcMap().empty()) {
        Ptr<mmwave::MmWaveComponentCarrierEnb> cc = DynamicCast<mmwave::MmWaveComponentCarrierEnb>(enbDev->GetCcMap().at(0));
        if (cc && cc->GetMacScheduler()) { cachedFlexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(cc->GetMacScheduler()); schedulerCached = true; }
      }
    }
    if (cachedFlexSched) { mcsDl = cachedFlexSched->GetCurrentMcsDl(); mcsUl = cachedFlexSched->GetCurrentMcsUl(); }
    f << "," << pingMs << "," << static_cast<int>(mcsDl) << "," << static_cast<int>(mcsUl) << "," << g_activeMcsEvent;
    f << (g_activeBlockage ? ",IMSI_" + std::to_string(g_blockageImsi) : ",None") << (g_activeTrafficSpike ? ",RH_" + std::to_string(g_trafficSpikeRhIdx) : ",None") << "\n";
    if (++sampleCount % 4 == 0) f.flush();
  }
  Simulator::Schedule(Seconds(periodSec), &SampleAll, ueNodes, ueDevs, gnbNode, covRadius, sinkApps, periodSec);
}

// --- EXACT MCS LOGIC FROM YOUR WORKING CODE ---
static void ChangeMcs(Ptr<Node> gnb, int mcs) {
  Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnb->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>(); if (!enbDev) return;
  if (enbDev->GetCcMap().empty()) return;
  Ptr<mmwave::MmWaveComponentCarrierEnb> cc = DynamicCast<mmwave::MmWaveComponentCarrierEnb>(enbDev->GetCcMap().at(0)); if (!cc) return;
  Ptr<mmwave::MmWaveFlexTtiMacScheduler> flexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(cc->GetMacScheduler()); if (!flexSched) return;
  if (mcs >= 0) {
    flexSched->SetAttribute("FixedMcsDl", BooleanValue(true)); flexSched->SetAttribute("McsDefaultDl", UintegerValue(mcs));
    flexSched->SetAttribute("FixedMcsUl", BooleanValue(true)); flexSched->SetAttribute("McsDefaultUl", UintegerValue(mcs));
  } else {
    flexSched->SetAttribute("FixedMcsDl", BooleanValue(false)); flexSched->SetAttribute("FixedMcsUl", BooleanValue(false));
  }
}

// --- DYNAMIC CHAOS EVENTS FOR RL TRAINING ---
static void TriggerBlockageEvent(NodeContainer ues, double durationS) {
    uint32_t ueIdx = rand() % ues.GetN(); Ptr<Node> ue = ues.Get(ueIdx);
    Ptr<mmwave::MmWaveUeNetDevice> ueDev = nullptr;
    for (uint32_t i = 0; i < ue->GetNDevices(); ++i) { ueDev = ue->GetDevice(i)->GetObject<mmwave::MmWaveUeNetDevice>(); if (ueDev) break; }
    if (ueDev && ueDev->GetPhy()) {
        Ptr<mmwave::MmWaveUePhy> phy = ueDev->GetPhy();
        double originalNf = phy->GetNoiseFigure();
        phy->SetNoiseFigure(originalNf + 60.0);
        g_activeBlockage = true; g_blockageImsi = ueDev->GetImsi();
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [CHAOS] Blockage on UE " << g_blockageImsi << " for " << durationS << "s");
        Simulator::Schedule(Seconds(durationS), [phy, originalNf]() { phy->SetNoiseFigure(originalNf); g_activeBlockage = false; });
    }
}

static void TriggerTrafficSpikeEvent(NodeContainer remoteHosts, double durationS) {
    uint32_t rhIdx = rand() % remoteHosts.GetN(); Ptr<Node> rh = remoteHosts.Get(rhIdx);
    std::vector<Ptr<OnOffApplication>> onOffApps;
    for (uint32_t i = 0; i < rh->GetNApplications(); ++i) {
        auto app = DynamicCast<OnOffApplication>(rh->GetApplication(i)); if (app) onOffApps.push_back(app);
    }
    if (!onOffApps.empty()) {
        auto onOffApp = onOffApps[rand() % onOffApps.size()];
        DataRate originalRate("50Mbps"); DataRate spikeRate("500Mbps");
        onOffApp->SetAttribute("DataRate", DataRateValue(spikeRate));
        g_activeTrafficSpike = true; g_trafficSpikeRhIdx = rhIdx;
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [CHAOS] Spike on RH " << rhIdx << " for " << durationS << "s");
        Simulator::Schedule(Seconds(durationS), [onOffApp, originalRate]() { onOffApp->SetAttribute("DataRate", DataRateValue(originalRate)); g_activeTrafficSpike = false; });
    }
}

static void TriggerMcsDropEvent(Ptr<Node> gnb, double durationS) {
    int targetMcs = 4 + (rand() % 6); // Random MCS between 4 and 9
    ChangeMcs(gnb, targetMcs);
    g_activeMcsEvent = "MCS_" + std::to_string(targetMcs);
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [CHAOS] MCS Drop to " << targetMcs << " for " << durationS << "s");
    Simulator::Schedule(Seconds(durationS), [gnb]() { ChangeMcs(gnb, -1); g_activeMcsEvent = "None"; });
}

// Randomly triggers events continuously to feed the RL engine
static void ScheduleRandomEvent(Ptr<UniformRandomVariable> rng, NodeContainer ues, Ptr<Node> gnb, NodeContainer rh) {
    if (Simulator::Now().GetSeconds() >= g_simStopTime - 60.0) return;
    
    double duration = rng->GetValue(15.0, 45.0); // 15 to 45 sec event
    double cooldown = rng->GetValue(20.0, 60.0); // 20 to 60 sec wait between events
    int eventType = rng->GetInteger(0, 3);
    
    switch(eventType) {
        case 0: TriggerBlockageEvent(ues, duration); break;
        case 1: TriggerTrafficSpikeEvent(rh, duration); break;
        case 2: TriggerMcsDropEvent(gnb, duration); break;
        case 3: TriggerBlockageEvent(ues, duration); TriggerTrafficSpikeEvent(rh, duration); break; // Double failure
    }
    
    Simulator::Schedule(Seconds(duration + cooldown), &ScheduleRandomEvent, rng, ues, gnb, rh);
}

// ---------------- MAIN ----------------
int main (int argc, char** argv) {
  CommandLine cmd;
  int rngSeed = 42;
  cmd.AddValue("rngSeed", "Seed for random number generator", rngSeed);
  cmd.Parse(argc, argv);
  srand(rngSeed);

  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  g_simStopTime = simV.Get();
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get(); fs::create_directories(outDir); fs::current_path(outDir);

  // EXACT initialization from your code
  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue(0.5));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2Periodicity", DoubleValue(0.5));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue("10.0.2.10"));
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(56e6));
  
  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  mmw->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(2);
  NodeContainer rh;  rh.Create(1);
  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  MobilityHelper m; m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  auto enbPos = CreateObject<ListPositionAllocator>(); enbPos->Add(Vector(25,25,10));
  m.SetPositionAllocator(enbPos); m.Install(gnb);

  MobilityHelper uem; uem.SetMobilityModel("ns3::WaypointMobilityModel"); uem.Install(ue);
  
  // Exact UE Mobility loop from your working code
  Ptr<WaypointMobilityModel> ue0Mobility = ue.Get(0)->GetObject<WaypointMobilityModel>();
  double t_cycle = 0.0; double cycle_duration = 55.0;
  Vector p_start(30,25,1.5), p_bypass_top(45,60,1.5), p_bypass_side(65,60,1.5);
  Vector p_behind_wall(70,25,1.5), p_far_corner(95,110,1.5), p_clear(50,110,1.5);
  ue0Mobility->AddWaypoint(Waypoint(Seconds(0.0), p_start));
  while (t_cycle < g_simStopTime) {
      if (t_cycle + 10.0 > g_simStopTime) break; ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 10.0), p_bypass_top));
      if (t_cycle + 15.0 > g_simStopTime) break; ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 15.0), p_bypass_side));
      if (t_cycle + 25.0 > g_simStopTime) break; ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 25.0), p_behind_wall));
      if (t_cycle + 35.0 > g_simStopTime) break; ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 35.0), p_far_corner));
      if (t_cycle + 45.0 > g_simStopTime) break; ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 45.0), p_clear));
      if (t_cycle + 55.0 <= g_simStopTime)       ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 55.0), p_start));
      t_cycle += cycle_duration;
  }

  Ptr<WaypointMobilityModel> ue1Mobility = ue.Get(1)->GetObject<WaypointMobilityModel>();
  double t_ue1 = 0.0; bool movingOutward = false;
  while (t_ue1 < g_simStopTime) {
      ue1Mobility->AddWaypoint(Waypoint(Seconds(t_ue1), Vector(movingOutward ? 145.0 : 25.0, 55.0, 1.5)));
      t_ue1 += 15.0; movingOutward = !movingOutward; 
  }

  MobilityHelper coreMobility; coreMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  Ptr<ListPositionAllocator> cp = CreateObject<ListPositionAllocator>();
  cp->Add(Vector(20,25,0)); cp->Add(Vector(20,30,0)); cp->Add(Vector(20,20,0));
  coreMobility.SetPositionAllocator(cp);
  NodeContainer coreNodes; coreNodes.Add(pgw); coreNodes.Add(NodeList::GetNode(1)); coreNodes.Add(rh.Get(0));
  coreMobility.Install(coreNodes);

  Ptr<Building> wall = CreateObject<Building>();
  wall->SetBoundaries(Box(50.0, 55.0, 0.0, 50.0, 0.0, 10.0)); 
  wall->SetBuildingType(Building::Commercial); wall->SetExtWallsType(Building::ConcreteWithWindows);
  wall->SetNRoomsX(1); wall->SetNRoomsY(1); wall->SetNFloors(1);
  BuildingsHelper::Install(gnb); BuildingsHelper::Install(ue);

  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ue.GetN(); ++u) srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>())->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  PointToPointHelper p2p; p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s"))); p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  auto d = p2p.Install(pgw, rh.Get(0));
  Ipv4AddressHelper a; a.SetBase("10.0.0.0","255.0.0.0"); a.Assign(d);
  Ipv4StaticRoutingHelper srh; srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  ApplicationContainer allSinks;
  for (uint32_t i = 0; i < ue.GetN(); ++i) {
      PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 4000 + i));
      allSinks.Add(sink.Install(ue.Get(i)));
      OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(i), 4000 + i));
      source.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
      source.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));
      source.SetAttribute("DataRate", DataRateValue(DataRate("50Mbps")));
      source.Install(rh.Get(0)).Start(Seconds(1.0 + (i * 0.1)));
  }
  allSinks.Start(Seconds(0.5));

  V4PingHelper ping(ueIf.GetAddress(0));
  ping.SetAttribute("Interval", TimeValue(Seconds(0.1)));
  ping.SetAttribute("Size", UintegerValue(56));
  ping.SetAttribute("Verbose", BooleanValue(false));
  ping.Install(rh.Get(0)).Start(Seconds(0.5));
  Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt", MakeCallback(&PingRttCallback));

  Simulator::Schedule(Seconds(1.0), &SampleAll, std::ref(ue), std::ref(ueDevs), gnb.Get(0), 100.0, allSinks, 0.5);

  // START THE RANDOM CHAOS ENGINE at t=30s
  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
  Simulator::Schedule(Seconds(30.0), &ScheduleRandomEvent, rng, ue, gnb.Get(0), rh);

  NS_LOG_UNCOND("Starting 1-Hour Continuous Chaos Training Scenario");
  Simulator::Stop(Seconds(g_simStopTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
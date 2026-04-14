/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * CONTINUOUS MOBILITY SCENARIO — RL TRAINING
 * Simulates extended training episodes. UEs move into distinct RF states 
 * (Clear LOS, Edge of Cell, Severe Blockage) and HOLD those states for 60s
 * to allow the RL agent to take actions and observe stabilized rewards.
 */

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
#include "ns3/ping-helper.h" 
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
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

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("ContinuousMobilityTraining");

static GlobalValue g_simTime("simTime", "Simulation time (s)", DoubleValue(36000.0), MakeDoubleChecker<double>(1.0, 86400.0));
bool g_enableCsvLogging = true;

struct GlobalState { double lastT = 0.0; std::vector<uint64_t> lastBytes; std::vector<double> ewma; bool seenPing = false; double lastPingMs = 0.0; } gS;
static void PingRttCallback(Time rtt) { gS.lastPingMs = rtt.GetMilliSeconds(); gS.seenPing = true; }

// --- CSV Sampler ---
static void SampleAll(NodeContainer ueNodes, NetDeviceContainer ueDevs, Ptr<Node> gnbNode, double covRadius, ApplicationContainer sinkApps, double periodSec) {
  static std::ofstream f; static bool headerDone = false; static uint32_t sampleCount = 0;
  static Ptr<mmwave::MmWaveFlexTtiMacScheduler> cachedFlexSched = nullptr; static bool schedulerCached = false;
  if (g_enableCsvLogging) {
    if (!headerDone) {
      f.open("sim_timeseries_continuous_mobility.csv", std::ios::out | std::ios::trunc);
      f << std::fixed << std::setprecision(6); f << "time_s";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) f << ",ue" << i << "_imsi,ue" << i << "_x,ue" << i << "_y,ue" << i << "_z,ue" << i << "_dist_to_gnb_m,ue" << i << "_inside";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) f << ",throughput_ue" << i << "_mbps";
      f << ",ping_ms,mcs_dl,mcs_ul\n";
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
    f << "," << pingMs << "," << static_cast<int>(mcsDl) << "," << static_cast<int>(mcsUl) << "\n";
    if (++sampleCount % 4 == 0) f.flush();
  }
  Simulator::Schedule(Seconds(periodSec), &SampleAll, ueNodes, ueDevs, gnbNode, covRadius, sinkApps, periodSec);
}

// ---------------- MAIN ----------------
int main(int argc, char** argv) {
  CommandLine cmd; 
  int rngSeed = 1; 
  cmd.AddValue("rngSeed", "Seed for random number generator", rngSeed);
  cmd.Parse(argc, argv);
  
  srand(rngSeed);
  RngSeedManager::SetSeed(rngSeed);

  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simStopTime = simV.Get();

  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue(0.5));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2Periodicity", DoubleValue(0.5));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue("10.0.2.10"));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue(true));

  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(56e6));

  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  mmw->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::BuildingsChannelConditionModel");
  
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(2);
  NodeContainer rh;  rh.Create(1);
  Ptr<Node> pgw = epc->GetPgwNode();
  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  MobilityHelper m; m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  auto pos = CreateObject<ListPositionAllocator>(); pos->Add(Vector(25, 25, 10)); // gNB at center
  m.SetPositionAllocator(pos); m.Install(gnb);

  // --- RL TRAINING MOBILITY (HOLDING STATES) ---
  MobilityHelper uem; uem.SetMobilityModel("ns3::WaypointMobilityModel"); uem.Install(ue);
  auto ue0m = ue.Get(0)->GetObject<WaypointMobilityModel>();
  auto ue1m = ue.Get(1)->GetObject<WaypointMobilityModel>();
  
  double t = 0;
  ue0m->AddWaypoint(Waypoint(Seconds(0), Vector(25,25,1.5)));
  ue1m->AddWaypoint(Waypoint(Seconds(0), Vector(25,60,1.5)));

  // Cycle is 300 seconds
  while(t < simStopTime) {
      // FIXED UE0: Slow Fading Scenario (Clear Line of Sight)
      // Walks straight North to avoid walls entirely.
      ue0m->AddWaypoint(Waypoint(Seconds(t + 120.0), Vector(25, 150, 1.5)));  // Walk out North
      ue0m->AddWaypoint(Waypoint(Seconds(t + 180.0), Vector(25, 150, 1.5)));  // Hold bad coverage
      ue0m->AddWaypoint(Waypoint(Seconds(t + 300.0), Vector(25, 25, 1.5)));   // Walk back

      // UE1: Severe Blockage Scenario
      // Moves behind concrete wall, holds for 60s, moves to clear LOS, holds for 60s.
      ue1m->AddWaypoint(Waypoint(Seconds(t + 10.0), Vector(25, 60, 1.5)));  // Move to clear LOS
      ue1m->AddWaypoint(Waypoint(Seconds(t + 70.0), Vector(25, 60, 1.5)));  // Hold clear LOS
      ue1m->AddWaypoint(Waypoint(Seconds(t + 80.0), Vector(70, 25, 1.5)));  // Move behind Wall 1
      ue1m->AddWaypoint(Waypoint(Seconds(t + 140.0), Vector(70, 25, 1.5))); // Hold behind Wall 1
      ue1m->AddWaypoint(Waypoint(Seconds(t + 150.0), Vector(25, -20, 1.5)));// Move to clear LOS
      ue1m->AddWaypoint(Waypoint(Seconds(t + 210.0), Vector(25, -20, 1.5)));// Hold clear LOS
      ue1m->AddWaypoint(Waypoint(Seconds(t + 220.0), Vector(70, 70, 1.5))); // Move behind Wall 2
      ue1m->AddWaypoint(Waypoint(Seconds(t + 280.0), Vector(70, 70, 1.5))); // Hold behind Wall 2
      ue1m->AddWaypoint(Waypoint(Seconds(t + 300.0), Vector(25, 60, 1.5))); // Return
      
      t += 300.0; 
  }

  // Set up walls and give them concrete material to attenuate signals
  Ptr<Building> wall1 = CreateObject<Building>();
  wall1->SetBoundaries(Box(50, 55, -50, 50, 0, 20)); // Wall blocking the East
  wall1->SetBuildingType(Building::Commercial); 
  wall1->SetExtWallsType(Building::ConcreteWithWindows);
  
  Ptr<Building> wall2 = CreateObject<Building>();
  wall2->SetBoundaries(Box(50, 150, 65, 70, 0, 20)); // Wall blocking the North-East
  wall2->SetBuildingType(Building::Commercial); 
  wall2->SetExtWallsType(Building::ConcreteWithWindows);
  
  BuildingsHelper::Install(gnb); BuildingsHelper::Install(ue);

  // Core & Backhaul setup
  MobilityHelper cm; cm.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  auto cp = CreateObject<ListPositionAllocator>(); cp->Add(Vector(20,25,0)); cp->Add(Vector(20,30,0)); cp->Add(Vector(20,20,0));
  cm.SetPositionAllocator(cp); cm.Install(NodeContainer(pgw, NodeList::GetNode(1), rh.Get(0)));

  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u = 0; u < ue.GetN(); ++u) srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>())->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  PointToPointHelper p2p; p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s"))); p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  auto d = p2p.Install(pgw, rh.Get(0));
  Ipv4AddressHelper a; a.SetBase("10.0.0.0","255.0.0.0"); a.Assign(d);
  Ipv4StaticRoutingHelper srh; srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  std::string normalRate = "50Mbps";
  ApplicationContainer allSinks;
  for (uint32_t i = 0; i < ue.GetN(); ++i) {
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 4000+i));
    allSinks.Add(sink.Install(ue.Get(i)));
    OnOffHelper src("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(i), 4000+i));
    src.SetAttribute("OnTime",  StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
    src.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));
    src.SetAttribute("DataRate", DataRateValue(DataRate(normalRate)));
    src.Install(rh.Get(0)).Start(Seconds(1.0));
  }
  allSinks.Start(Seconds(0.5));
  
  PingHelper ping(ueIf.GetAddress(0)); 
  ping.SetAttribute("Interval", TimeValue(Seconds(0.1)));
  ping.Install(rh.Get(0)).Start(Seconds(0.5));
  Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::Ping/Rtt", MakeCallback(&PingRttCallback));

  Simulator::Schedule(Seconds(1.0), &SampleAll, ue, ueDevs, gnb.Get(0), 100.0, allSinks, 0.5);

  mmw->EnableTraces();

  NS_LOG_UNCOND("Starting CONTINUOUS MOBILITY Training Scenario (" << simStopTime << "s)");
  Simulator::Stop(Seconds(simStopTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
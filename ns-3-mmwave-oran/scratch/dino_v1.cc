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
#include <cstdio> // for remove()

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_1gNB_4UE_v3");

// ---------------- Runtime flags ----------------
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(3599.0), MakeDoubleChecker<double>(1.0, 3600.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

bool g_enableCsvLogging = true;

static GlobalValue g_fastMode ("fastMode", "If true, disables E2 overhead and file logging",
                               BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_controlFileName ("controlFileName","The path to the control file (can be absolute)",
                                      StringValue(""), MakeStringChecker());
static GlobalValue g_e2nrEnabled ("e2nrEnabled","If true, send NR E2 reports",
                                  BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2du ("e2du","If true, send DU reports",
                           BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuUp ("e2cuUp","If true, send CU-UP reports",
                             BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuCp ("e2cuCp","If true, send CU-CP reports",
                             BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_indicationPeriodicity ("indicationPeriodicity","E2 Indication Periodicity (s)", 
                                            DoubleValue(0.5), MakeDoubleChecker<double>(0.01, 2.0));

static GlobalValue g_enableE2FileLogging ("enableE2FileLogging","Offline file logging instead of connecting to RIC",
                                          BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_reducedPmValues ("reducedPmValues", "If true, use a subset of the pm containers",
                                      BooleanValue(false), MakeBooleanChecker());

static GlobalValue g_e2lteEnabled ("e2lteEnabled","If true, send LTE E2 reports",
                                   BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2TermIp ("e2TermIp","RIC E2 termination IP",
                               StringValue("10.0.2.10"), MakeStringChecker());
static GlobalValue q_useSemaphores ("useSemaphores","If true, enables the use of semaphores for external environment control",
    BooleanValue(false), MakeBooleanChecker());

// ---------------- Timeseries sampler & Globals ----------------
struct GlobalState {
  double lastT = 0.0;
  std::vector<uint64_t> lastBytes;
  std::vector<double> ewma;
  bool seenPing = false;
  double lastPingMs = 0.0;
} gS;

// Event tracking globals
static std::string g_activeMcsEvent = "None";
static bool g_activeBlockage = false;
static uint64_t g_blockageImsi = 0;
static bool g_activeTrafficSpike = false;
static uint32_t g_trafficSpikeRhIdx = 0;

AnimationInterface *g_anim = nullptr;
static double g_simStopTime = 0.0;

static void PingRttCallback(Time rtt) {
  gS.lastPingMs = rtt.GetMilliSeconds();
  gS.seenPing   = true;
}

static void SampleAll(const NodeContainer &ueNodes,
                      const NetDeviceContainer &ueDevs,
                      Ptr<Node> gnbNode,
                      double covRadius,
                      ApplicationContainer sinkApps,
                      double periodSec)
{
  static std::ofstream f;
  static bool headerDone = false;
  static uint32_t sampleCount = 0;
  static Ptr<mmwave::MmWaveFlexTtiMacScheduler> cachedFlexSched = nullptr;
  static bool schedulerCached = false;
  if (g_enableCsvLogging) {
    if (!headerDone) {
      f.open("sim_timeseries_v3.csv", std::ios::out | std::ios::trunc);
      f << std::fixed << std::setprecision(6);
      f << "time_s";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        f << ",ue" << i << "_imsi"
          << ",ue" << i << "_x"
          << ",ue" << i << "_y"
          << ",ue" << i << "_z"
          << ",ue" << i << "_dist_to_gnb_m"
          << ",ue" << i << "_inside";
      }
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        f << ",throughput_ue" << i << "_mbps";
      }
      f << ",ping_ms"
        << ",mcs_dl"
        << ",mcs_ul"
        << ",event_mcs_type"
        << ",event_blockage"
        << ",event_traffic_spike"
        << "\n";
      headerDone = true;
    }

    const double now = Simulator::Now().GetSeconds();
    Vector gp = gnbNode->GetObject<MobilityModel>()->GetPosition();
    f << now;

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
      Vector p = ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
      const double dx = p.x - gp.x, dy = p.y - gp.y, dz = p.z - gp.z;
      const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
      const int inside = (dist <= covRadius ? 1 : 0);
      uint64_t imsi = ueDevs.Get(i)->GetObject<MmWaveUeNetDevice>()->GetImsi();

      f << "," << imsi
        << "," << p.x
        << "," << p.y
        << "," << p.z
        << "," << dist
        << "," << inside;
    }

    if (gS.lastBytes.size() < sinkApps.GetN()) {
        gS.lastBytes.resize(sinkApps.GetN(), 0);
        gS.ewma.resize(sinkApps.GetN(), 0.0);
    }

    double dt = now - gS.lastT;
    const double tau = 1.0; 
    const double alpha = 1.0 - std::exp(-(periodSec / tau));

    for (uint32_t i = 0; i < sinkApps.GetN(); ++i) {
        double mbps = 0.0;
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        if (sink) {
            uint64_t bytes = sink->GetTotalRx();
            if (gS.lastT > 0.0 && dt > 0.0) {
                mbps = 8.0 * (bytes - gS.lastBytes[i]) / dt / 1e6;
            }
            gS.lastBytes[i] = bytes;
            gS.ewma[i] = alpha * mbps + (1.0 - alpha) * gS.ewma[i];
            
            f << "," << mbps;

            if (g_anim && i < ueNodes.GetN()) {
                std::ostringstream oss;
                oss << "UE" << i << " (" << std::fixed << std::setprecision(1) << mbps << " Mbps)";
                g_anim->UpdateNodeDescription(ueNodes.Get(i), oss.str());
            }
        } else {
            f << ",0.0";
        }
    }
    gS.lastT = now;

    // REVERTED to old logic: Always print last known ping so UI doesn't flatline
    double pingMs = gS.seenPing ? gS.lastPingMs : 0.0;
    
    uint8_t mcsDl = 255;
    uint8_t mcsUl = 255;

    // Cache the scheduler pointer on first successful lookup to avoid
    // 7 levels of indirection + DynamicCast every 0.5s
    if (!schedulerCached) {
      Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnbNode->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
      if (enbDev) {
        std::map<uint8_t, Ptr<mmwave::MmWaveComponentCarrier>> ccMap = enbDev->GetCcMap();
        if (!ccMap.empty()) {
          Ptr<mmwave::MmWaveComponentCarrierEnb> cc = DynamicCast<mmwave::MmWaveComponentCarrierEnb>(ccMap.at(0));
          if (cc) {
            Ptr<mmwave::MmWaveMacScheduler> sched = cc->GetMacScheduler();
            if (sched) {
              cachedFlexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(sched);
              schedulerCached = true;
            }
          }
        }
      }
    }
    if (cachedFlexSched) {
      mcsDl = cachedFlexSched->GetCurrentMcsDl();
      mcsUl = cachedFlexSched->GetCurrentMcsUl();
    }

    f << "," << pingMs
      << "," << static_cast<int>(mcsDl) 
      << "," << static_cast<int>(mcsUl) 
      << "," << g_activeMcsEvent;

    if (g_activeBlockage) { f << ",IMSI_" << g_blockageImsi; } else { f << ",None"; }
    if (g_activeTrafficSpike) { f << ",RH_" << g_trafficSpikeRhIdx; } else { f << ",None"; }

    f << "\n";
    // Flush every 4 samples (~2s) to balance I/O overhead vs UI responsiveness
    if (++sampleCount % 4 == 0) {
      f.flush();
    }
  }

  Simulator::Schedule(Seconds(periodSec), &SampleAll,
                      ueNodes, ueDevs, gnbNode, covRadius, sinkApps, periodSec);
}

// ---------------- Dynamic MCS Logic ----------------
static void ChangeMcs(Ptr<Node> gnb, int mcs)
{
  Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnb->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
  if (!enbDev) return;

  std::map<uint8_t, Ptr<mmwave::MmWaveComponentCarrier>> ccMap = enbDev->GetCcMap();
  if (ccMap.empty()) return;

  Ptr<mmwave::MmWaveComponentCarrierEnb> cc = DynamicCast<mmwave::MmWaveComponentCarrierEnb>(ccMap.at(0));
  if (!cc) return;

  Ptr<mmwave::MmWaveMacScheduler> sched = cc->GetMacScheduler();
  if (!sched) return;

  Ptr<mmwave::MmWaveFlexTtiMacScheduler> flexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(sched);
  if (flexSched) {
    if (mcs >= 0) {
      flexSched->SetAttribute("FixedMcsDl", BooleanValue(true));
      flexSched->SetAttribute("McsDefaultDl", UintegerValue(mcs));
      flexSched->SetAttribute("FixedMcsUl", BooleanValue(true));
      flexSched->SetAttribute("McsDefaultUl", UintegerValue(mcs));
    } else {
      flexSched->SetAttribute("FixedMcsDl", BooleanValue(false));
      flexSched->SetAttribute("FixedMcsUl", BooleanValue(false));
    }
  }
}

static int GetCurrentMcs(Ptr<Node> gnb)
{
  Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnb->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
  if (!enbDev) return -1;
  std::map<uint8_t, Ptr<mmwave::MmWaveComponentCarrier>> ccMap = enbDev->GetCcMap();
  if (ccMap.empty()) return -1;
  Ptr<mmwave::MmWaveComponentCarrierEnb> cc = DynamicCast<mmwave::MmWaveComponentCarrierEnb>(ccMap.at(0));
  if (!cc) return -1;
  Ptr<mmwave::MmWaveMacScheduler> sched = cc->GetMacScheduler();
  if (!sched) return -1;
  Ptr<mmwave::MmWaveFlexTtiMacScheduler> flexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(sched);
  if (flexSched) {
    return flexSched->GetCurrentMcsDl();
  }
  return -1;
}

// ---------------- TRIGGERED EVENTS (No Random Spawning) ----------------

static void TriggerBlockageEvent(NodeContainer ues, Ptr<Node> gnb)
{
    uint32_t ueIdx = rand() % ues.GetN();
    Ptr<Node> ue = ues.Get(ueIdx);
    
    Ptr<mmwave::MmWaveUeNetDevice> ueDev = nullptr;
    for (uint32_t i = 0; i < ue->GetNDevices(); ++i) {
        ueDev = ue->GetDevice(i)->GetObject<mmwave::MmWaveUeNetDevice>();
        if (ueDev) break;
    }

    if (ueDev) {
        Ptr<mmwave::MmWaveUePhy> phy = ueDev->GetPhy();
        if (phy) {
            double originalNf = phy->GetNoiseFigure();
            double blockageNf = originalNf + 60.0; 
            
            phy->SetNoiseFigure(blockageNf);
            uint64_t imsi = ueDev->GetImsi();
            g_activeBlockage = true;
            g_blockageImsi = imsi;

            // Restore after 5 seconds
            Simulator::Schedule(Seconds(5.0), [phy, originalNf, ueDev, imsi, ueIdx]() {
                phy->SetNoiseFigure(originalNf);
                g_activeBlockage = false;
            });
        }
    }
}

static void TriggerTrafficSpikeEvent(NodeContainer remoteHosts)
{
    uint32_t rhIdx = rand() % remoteHosts.GetN();
    Ptr<Node> rh = remoteHosts.Get(rhIdx);
    
    std::vector<Ptr<OnOffApplication>> onOffApps;
    for (uint32_t i = 0; i < rh->GetNApplications(); ++i) {
        auto app = DynamicCast<OnOffApplication>(rh->GetApplication(i));
        if (app) onOffApps.push_back(app);
    }
    
    Ptr<OnOffApplication> onOffApp = nullptr;
    if (!onOffApps.empty()) {
        onOffApp = onOffApps[rand() % onOffApps.size()];
    }
    
    if (onOffApp) {
        DataRate originalRate("50Mbps"); 
        DataRate spikeRate("500Mbps");   
        
        onOffApp->SetAttribute("DataRate", DataRateValue(spikeRate));
        g_activeTrafficSpike = true;
        g_trafficSpikeRhIdx = rhIdx;

        Simulator::Schedule(Seconds(5.0), [onOffApp, originalRate, rhIdx]() {
            onOffApp->SetAttribute("DataRate", DataRateValue(originalRate));
            g_activeTrafficSpike = false;
        });
    }
}

static void PollChaosTrigger(NodeContainer ues, Ptr<Node> gnb, NodeContainer remoteHosts)
{
    std::ifstream infile("/tmp/chaos_trigger.txt");
    if (infile.good()) {
        std::string command;
        // Read the entire line instead of just the first word
        std::getline(infile, command);
        infile.close();
        
        std::remove("/tmp/chaos_trigger.txt");

        if (command.find("BLOCKAGE") != std::string::npos || command.find("PACKET_LOSS") != std::string::npos) {
            TriggerBlockageEvent(ues, gnb);
        }
        else if (command.find("SPIKE") != std::string::npos) {
            TriggerTrafficSpikeEvent(remoteHosts);
        }
    }

    // Check again in 1 second
    if (Simulator::Now().GetSeconds() < g_simStopTime) {
        Simulator::Schedule(Seconds(1.0), &PollChaosTrigger, ues, gnb, remoteHosts);
    }
}

// ---------------- main ----------------
int main (int argc, char** argv)
{
  CommandLine cmd;
  int rngSeed = 0;
  cmd.AddValue("rngSeed", "Seed for random number generator (default 0 = random)", rngSeed);
  cmd.AddValue("enableLogging", "Enable CSV logging", g_enableCsvLogging);
  cmd.Parse(argc, argv);

  if (rngSeed == 0) {
    srand(time(NULL));
  } else {
    srand(rngSeed);
  }

  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simTime = simV.Get();
  g_simStopTime = simTime;
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();

  BooleanValue booleanValue;
  StringValue stringValue;
  DoubleValue doubleValue;

  GlobalValue::GetValueByName ("useSemaphores", booleanValue);
  bool useSemaphores = booleanValue.Get ();
  GlobalValue::GetValueByName ("controlFileName", stringValue);
  std::string controlFilename = stringValue.Get ();
  GlobalValue::GetValueByName ("e2lteEnabled", booleanValue);
  bool e2lteEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2nrEnabled", booleanValue);
  bool e2nrEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2du", booleanValue);
  bool e2du = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuUp", booleanValue);
  bool e2cuUp = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuCp", booleanValue);
  bool e2cuCp = booleanValue.Get ();
  GlobalValue::GetValueByName ("reducedPmValues", booleanValue);
  bool reducedPmValues = booleanValue.Get ();
  GlobalValue::GetValueByName ("indicationPeriodicity", doubleValue);
  double indicationPeriodicity = doubleValue.Get ();
  GlobalValue::GetValueByName ("e2TermIp", stringValue);
  std::string e2TermIp = stringValue.Get ();
  GlobalValue::GetValueByName ("enableE2FileLogging", booleanValue);
  bool enableE2FileLogging = booleanValue.Get ();

  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte", BooleanValue(e2lteEnabled));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr", BooleanValue(e2nrEnabled));
  Config::SetDefault("ns3::MmWaveHelper::E2Periodicity", DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue(e2TermIp));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue(e2du));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue(e2cuUp));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue(e2cuCp));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableE2FileLogging", BooleanValue(enableE2FileLogging));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue(reducedPmValues));

  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(56e6));
  Config::SetDefault("ns3::MmWaveEnbPhy::TxPower",          DoubleValue(10.0));
  Config::SetDefault("ns3::MmWaveUePhy::NoiseFigure",       DoubleValue(7.0));

  fs::create_directories(outDir);
  fs::current_path(outDir);

  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);

  mmw->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::BuildingsChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();

  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(2);
  NodeContainer rh;  rh.Create(1);

  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  const Vector gnbPos = Vector(25,25,10);
  {
    MobilityHelper m;
    auto enbPos = CreateObject<ListPositionAllocator>();
    enbPos->Add(gnbPos);
    m.SetPositionAllocator(enbPos);
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    m.Install(gnb);
  }

  MobilityHelper uem;
  uem.SetMobilityModel("ns3::WaypointMobilityModel");
  uem.Install(ue);

  // ---------------- UE 0 Mobility ----------------
  Ptr<WaypointMobilityModel> ue0Mobility = ue.Get(0)->GetObject<WaypointMobilityModel>();
  double t_cycle = 0.0;
  double cycle_duration = 55.0; // Increased to 55 to account for the extra waypoint

  Vector p_start(30, 25, 1.5);      
  Vector p_bypass_top(45, 60, 1.5); 
  // NEW: Move past the wall's X-boundary (55) while staying safely above its Y-boundary (50)
  Vector p_bypass_side(65, 60, 1.5); 
  Vector p_behind_wall(70, 25, 1.5); 
  Vector p_far_corner(95, 110, 1.5); 
  Vector p_clear(50, 110, 1.5);      

  ue0Mobility->AddWaypoint(Waypoint(Seconds(0.0), p_start));
  while (t_cycle < simTime) {
      if (t_cycle + 10.0 > simTime) break;
      ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 10.0), p_bypass_top));
      
      if (t_cycle + 15.0 > simTime) break;
      ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 15.0), p_bypass_side));

      if (t_cycle + 25.0 > simTime) break;
      ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 25.0), p_behind_wall));
      
      if (t_cycle + 35.0 > simTime) break;
      ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 35.0), p_far_corner));
      
      if (t_cycle + 45.0 > simTime) break;
      ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 45.0), p_clear));
      
      if (t_cycle + 55.0 <= simTime) {
            ue0Mobility->AddWaypoint(Waypoint(Seconds(t_cycle + 55.0), p_start));
      }
      t_cycle += cycle_duration;
  }

  // UE 1
  Ptr<WaypointMobilityModel> ue1Mobility = ue.Get(1)->GetObject<WaypointMobilityModel>();
  double t_ue1 = 0.0;
  bool movingOutward = false;

  // Change Y from 35.0 to 55.0 to safely clear the top edge of the wall (which stops at Y=50)
  while (t_ue1 < simTime) {
      double targetX = movingOutward ? 145.0 : 25.0;
      ue1Mobility->AddWaypoint(Waypoint(Seconds(t_ue1), Vector(targetX, 55.0, 1.5)));
      
      t_ue1 += 15.0; 
      movingOutward = !movingOutward; 
  }

  {
    Ptr<Node> sgw = NodeList::GetNode(1);
    NodeContainer stationaryCoreNodes; 
    stationaryCoreNodes.Add(pgw); 
    stationaryCoreNodes.Add(sgw); 
    stationaryCoreNodes.Add(rh.Get(0));
    
    MobilityHelper coreMobility; 
    coreMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> corePositions = CreateObject<ListPositionAllocator>();
    corePositions->Add(Vector(20.0,25.0,0.0)); 
    corePositions->Add(Vector(20.0,30.0,0.0)); 
    corePositions->Add(Vector(20.0,20.0,0.0)); 
    coreMobility.SetPositionAllocator(corePositions);
    coreMobility.Install(stationaryCoreNodes);
  }

  // Create a physical wall between the gNB and the UE's far waypoints
  Ptr<Building> wall = CreateObject<Building>();
  wall->SetBoundaries(Box(50.0, 55.0, 0.0, 50.0, 0.0, 10.0)); 

  // Give the wall material properties that attenuate RF signals
  wall->SetBuildingType(Building::Commercial);
  wall->SetExtWallsType(Building::ConcreteWithWindows);
  wall->SetNRoomsX(1);
  wall->SetNRoomsY(1);
  wall->SetNFloors(1);

  BuildingsHelper::Install(gnb);
  BuildingsHelper::Install(ue);

  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);

  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ue.GetN(); ++u) {
    Ptr<Ipv4StaticRouting> r = srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>());
    r->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  }

  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer d = p2p.Install(pgw, rh.Get(0));
  Ipv4AddressHelper a; a.SetBase("10.0.0.0","255.0.0.0");
  a.Assign(d);
  Ipv4StaticRoutingHelper srh;
  srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())
     ->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  const uint16_t basePort = 4000;
  ApplicationContainer allSinks;

  for (uint32_t i = 0; i < ue.GetN(); ++i) {
      PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
      allSinks.Add(sink.Install(ue.Get(i)));

      OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(i), basePort + i));
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
  ApplicationContainer pingApps = ping.Install(rh.Get(0));
  pingApps.Start(Seconds(0.5));
  
  Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt", MakeCallback(&PingRttCallback));

  mmw->EnableTraces();

  const double covRadius = 100.0;
  Simulator::Schedule(Seconds(1.0), &SampleAll,
                      std::ref(ue), std::ref(ueDevs), gnb.Get(0),
                      covRadius, allSinks, 0.5);

  AnimationInterface anim("NetAnimFile_v3.xml");
  g_anim = &anim;
  anim.SetMobilityPollInterval(Seconds(1));
  anim.SkipPacketTracing();

  Simulator::Schedule(Seconds(1.0), &PollChaosTrigger, ue, gnb.Get(0), rh);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

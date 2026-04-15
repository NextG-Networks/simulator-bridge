/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * NOVEL EVALUATION SCENARIO - 6 UE, Single gNB
 *
 * Topology:
 *   - 1 gNB at (25, 25, 10)
 *   - 1 concrete wall: X=[50,55], Y=[0,50], Z=[0,10]
 *   - 6 UEs with diverse mobility patterns (all avoid the wall)
 *   - 1 remote host generating 50 Mbps UDP per UE
 *
 * Wall-aware mobility:
 *   The wall blocks the corridor from west to east at ground level.
 *   All UE paths that cross X=50-55 do so at Y>50 (above the wall).
 *   UEs that stay on the west side never exceed X=48.
 *
 * Chaos events (injected randomly after warm-up):
 *   - Blockage: +80 dB noise figure on a random UE
 *   - Traffic spike: one UE's source jumps to 800 Mbps
 *   - Compound: blockage + spike simultaneously
 *   - NO MCS drop (conflicts with actuator control from the RIC)
 *
 * Channel model: ThreeGppUma (cheaper than UMi Street Canyon)
 *   + BuildingsChannelConditionModel (wall still causes NLOS)
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

#include <chrono>
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

NS_LOG_COMPONENT_DEFINE("Eval_Novel_6UE");

// ================================================================
//  SIMULATION TIME — change this one value to adjust run length
// ================================================================
static const double SIM_DURATION_S = 180.0;

static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(SIM_DURATION_S), MakeDoubleChecker<double>(1.0, 36000.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

bool g_enableCsvLogging = true;
static double g_simStopTime = 0.0;

// E2 / RIC configuration
static GlobalValue g_fastMode            ("fastMode",              "Disable E2 overhead",       BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_controlFileName     ("controlFileName",       "Path to control file",      StringValue(""),     MakeStringChecker());
static GlobalValue g_e2nrEnabled         ("e2nrEnabled",           "NR E2 reports",             BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_e2du                ("e2du",                  "DU reports",                BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_e2cuUp              ("e2cuUp",                "CU-UP reports",             BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_e2cuCp              ("e2cuCp",                "CU-CP reports",             BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_indicationPeriodicity("indicationPeriodicity","E2 Periodicity (s)",        DoubleValue(0.1),    MakeDoubleChecker<double>(0.01, 2.0));
static GlobalValue g_enableE2FileLogging ("enableE2FileLogging",   "Offline logging",           BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_reducedPmValues     ("reducedPmValues",       "Subset pm containers",      BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_e2lteEnabled        ("e2lteEnabled",          "LTE E2 reports",            BooleanValue(true),  MakeBooleanChecker());
static GlobalValue g_e2TermIp            ("e2TermIp",              "RIC IP",                    StringValue("10.0.2.10"), MakeStringChecker());
static GlobalValue q_useSemaphores       ("useSemaphores",         "Semaphores",                BooleanValue(false), MakeBooleanChecker());

// ================================================================
//  Global state for CSV sampling
// ================================================================
struct GlobalState {
  double lastT = 0.0;
  std::vector<uint64_t> lastBytes;
  std::vector<double> ewma;
  bool seenPing = false;
  double lastPingMs = 0.0;
} gS;

static std::string g_activeMcsEvent = "None";
static bool g_activeBlockage = false;
static uint64_t g_blockageImsi = 0;
static bool g_activeTrafficSpike = false;
static uint32_t g_trafficSpikeRhIdx = 0;
AnimationInterface *g_anim = nullptr;

// ================================================================
//  Progress printer — every 1 sim-second AND at least every 10 real seconds
// ================================================================
static auto g_wallStart = std::chrono::steady_clock::now();
static double g_lastPrintWall = 0.0;

static void PrintProgress() {
  auto elapsed = std::chrono::steady_clock::now() - g_wallStart;
  double wallSec = std::chrono::duration<double>(elapsed).count();
  double simSec = Simulator::Now().GetSeconds();
  double ratio = (wallSec > 0.0) ? (simSec / wallSec) : 0.0;
  double pct = (g_simStopTime > 0.0) ? (simSec / g_simStopTime * 100.0) : 0.0;

  // Print if ≥10 real seconds since last print, or always (1 sim-sec cadence)
  if ((wallSec - g_lastPrintWall) >= 10.0 || simSec <= 1.0 ||
      static_cast<int>(simSec) % 1 == 0) {
    std::cout << "  [SIM] t=" << std::fixed << std::setprecision(1)
      << simSec << "/" << g_simStopTime << "s  "
      << std::setprecision(0) << pct << "%"
      << "  (wall: " << (int)wallSec << "s, ratio: "
      << std::setprecision(3) << ratio << "x)" << std::endl;
    g_lastPrintWall = wallSec;
  }

  // Re-schedule every 1 sim-second
  Simulator::Schedule(Seconds(1.0), &PrintProgress);
}

// ================================================================
//  Ping RTT callback
// ================================================================
static void PingRttCallback(Time rtt) {
  gS.lastPingMs = rtt.GetMilliSeconds();
  gS.seenPing = true;
}

// ================================================================
//  CSV timeseries sampler
// ================================================================
static void SampleAll(const NodeContainer &ueNodes,
                      const NetDeviceContainer &ueDevs,
                      Ptr<Node> gnbNode, double covRadius,
                      ApplicationContainer sinkApps, double periodSec)
{
  static std::ofstream f;
  static bool headerDone = false;
  static uint32_t sampleCount = 0;
  static Ptr<mmwave::MmWaveFlexTtiMacScheduler> cachedFlexSched = nullptr;
  static bool schedulerCached = false;

  if (g_enableCsvLogging) {
    if (!headerDone) {
      f.open("sim_timeseries_eval_novel.csv", std::ios::out | std::ios::trunc);
      f << std::fixed << std::setprecision(6);
      f << "time_s";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        f << ",ue" << i << "_imsi,ue" << i << "_x,ue" << i << "_y,ue" << i << "_z"
          << ",ue" << i << "_dist_to_gnb_m,ue" << i << "_inside";
      for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        f << ",throughput_ue" << i << "_mbps";
      f << ",ping_ms,mcs_dl,mcs_ul,event_mcs_type,event_blockage,event_traffic_spike\n";
      headerDone = true;
    }

    const double now = Simulator::Now().GetSeconds();
    Vector gp = gnbNode->GetObject<MobilityModel>()->GetPosition();
    f << now;

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
      Vector p = ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
      double dist = std::sqrt(pow(p.x - gp.x, 2) + pow(p.y - gp.y, 2) + pow(p.z - gp.z, 2));
      f << "," << ueDevs.Get(i)->GetObject<MmWaveUeNetDevice>()->GetImsi()
        << "," << p.x << "," << p.y << "," << p.z
        << "," << dist << "," << (dist <= covRadius ? 1 : 0);
    }

    if (gS.lastBytes.size() < sinkApps.GetN()) {
      gS.lastBytes.resize(sinkApps.GetN(), 0);
      gS.ewma.resize(sinkApps.GetN(), 0.0);
    }

    double dt = now - gS.lastT;
    for (uint32_t i = 0; i < sinkApps.GetN(); ++i) {
      double mbps = 0.0;
      Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
      if (sink) {
        uint64_t bytes = sink->GetTotalRx();
        if (gS.lastT > 0.0 && dt > 0.0) mbps = 8.0 * (bytes - gS.lastBytes[i]) / dt / 1e6;
        gS.lastBytes[i] = bytes;
        f << "," << mbps;
      } else {
        f << ",0.0";
      }
    }
    gS.lastT = now;

    double pingMs = gS.seenPing ? gS.lastPingMs : 0.0;
    uint8_t mcsDl = 255, mcsUl = 255;

    if (!schedulerCached) {
      Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnbNode->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
      if (enbDev && !enbDev->GetCcMap().empty()) {
        Ptr<mmwave::MmWaveComponentCarrierEnb> cc =
          DynamicCast<mmwave::MmWaveComponentCarrierEnb>(enbDev->GetCcMap().at(0));
        if (cc && cc->GetMacScheduler()) {
          cachedFlexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(cc->GetMacScheduler());
          schedulerCached = true;
        }
      }
    }
    if (cachedFlexSched) {
      mcsDl = cachedFlexSched->GetCurrentMcsDl();
      mcsUl = cachedFlexSched->GetCurrentMcsUl();
    }

    f << "," << pingMs << "," << static_cast<int>(mcsDl) << "," << static_cast<int>(mcsUl)
      << "," << g_activeMcsEvent;
    f << (g_activeBlockage ? ",IMSI_" + std::to_string(g_blockageImsi) : ",None");
    f << (g_activeTrafficSpike ? ",RH_" + std::to_string(g_trafficSpikeRhIdx) : ",None");
    f << "\n";

    if (++sampleCount % 4 == 0) f.flush();
  }

  Simulator::Schedule(Seconds(periodSec), &SampleAll,
                      ueNodes, ueDevs, gnbNode, covRadius, sinkApps, periodSec);
}

// ================================================================
//  Chaos events (blockage + traffic spike only, NO MCS drop)
// ================================================================
static void TriggerBlockageEvent(NodeContainer ues, double durationS) {
  uint32_t ueIdx = rand() % ues.GetN();
  Ptr<Node> ue = ues.Get(ueIdx);
  Ptr<mmwave::MmWaveUeNetDevice> ueDev = nullptr;
  for (uint32_t i = 0; i < ue->GetNDevices(); ++i) {
    ueDev = ue->GetDevice(i)->GetObject<mmwave::MmWaveUeNetDevice>();
    if (ueDev) break;
  }
  if (ueDev && ueDev->GetPhy()) {
    Ptr<mmwave::MmWaveUePhy> phy = ueDev->GetPhy();
    double originalNf = phy->GetNoiseFigure();
    phy->SetNoiseFigure(originalNf + 80.0);
    g_activeBlockage = true;
    g_blockageImsi = ueDev->GetImsi();
    NS_LOG_UNCOND(Simulator::Now().GetSeconds()
      << "s: [NOVEL] SEVERE Blockage on UE " << g_blockageImsi
      << " (idx=" << ueIdx << ") for " << durationS << "s");
    Simulator::Schedule(Seconds(durationS), [phy, originalNf]() {
      phy->SetNoiseFigure(originalNf);
      g_activeBlockage = false;
    });
  }
}

static void TriggerTrafficSpikeEvent(NodeContainer remoteHosts, double durationS) {
  uint32_t rhIdx = rand() % remoteHosts.GetN();
  Ptr<Node> rh = remoteHosts.Get(rhIdx);
  std::vector<Ptr<OnOffApplication>> onOffApps;
  for (uint32_t i = 0; i < rh->GetNApplications(); ++i) {
    auto app = DynamicCast<OnOffApplication>(rh->GetApplication(i));
    if (app) onOffApps.push_back(app);
  }
  if (!onOffApps.empty()) {
    auto onOffApp = onOffApps[rand() % onOffApps.size()];
    DataRate originalRate("50Mbps");
    std::string spikeRate = "800Mbps";
    onOffApp->SetAttribute("DataRate", DataRateValue(DataRate(spikeRate)));
    g_activeTrafficSpike = true;
    g_trafficSpikeRhIdx = rhIdx;
    NS_LOG_UNCOND(Simulator::Now().GetSeconds()
      << "s: [NOVEL] MASSIVE Spike " << spikeRate
      << " on RH " << rhIdx << " for " << durationS << "s");
    Simulator::Schedule(Seconds(durationS), [onOffApp, originalRate]() {
      onOffApp->SetAttribute("DataRate", DataRateValue(originalRate));
      g_activeTrafficSpike = false;
    });
  }
}

// ================================================================
//  Rapid-fire random event scheduler
// ================================================================
static void ScheduleRandomEvent(Ptr<UniformRandomVariable> rng,
                                NodeContainer ues, Ptr<Node> gnb,
                                NodeContainer rh)
{
  if (Simulator::Now().GetSeconds() >= g_simStopTime - 30.0) return;

  double duration = rng->GetValue(5.0, 35.0);
  double cooldown = rng->GetValue(10.0, 40.0);
  int eventType = rng->GetInteger(0, 2);  // 0-2 only (no MCS drop)

  switch (eventType) {
    case 0: TriggerBlockageEvent(ues, duration); break;
    case 1: TriggerTrafficSpikeEvent(rh, duration); break;
    case 2: // Compound: blockage + spike simultaneously
      TriggerBlockageEvent(ues, duration);
      TriggerTrafficSpikeEvent(rh, duration);
      break;
  }

  Simulator::Schedule(Seconds(duration + cooldown),
                      &ScheduleRandomEvent, rng, ues, gnb, rh);
}

// ================================================================
//  Helper: add cyclic waypoints that repeat until simTime
// ================================================================
static void AddCyclicWaypoints(Ptr<WaypointMobilityModel> mob,
                               const std::vector<Vector> &points,
                               const std::vector<double> &offsets,
                               double cycleDuration, double simTime)
{
  double lastAdded = -1.0;  // track last timestamp to avoid duplicates
  double t_cycle = 0.0;
  while (t_cycle < simTime) {
    for (size_t i = 0; i < offsets.size(); ++i) {
      double t = t_cycle + offsets[i];
      if (t > simTime) return;
      if (t > lastAdded) {   // strictly increasing timestamps only
        mob->AddWaypoint(Waypoint(Seconds(t), points[i]));
        lastAdded = t;
      }
    }
    t_cycle += cycleDuration;
  }
}

// ================================================================
//  MAIN
// ================================================================
int main(int argc, char** argv) {
  std::cout << "[TEST] main() entered — stdout works" << std::endl;
  std::cerr << "[TEST] main() entered — stderr works" << std::endl;

  CommandLine cmd;
  int rngSeed = 42;
  cmd.AddValue("rngSeed", "Seed for random number generator", rngSeed);
  cmd.Parse(argc, argv);
  srand(rngSeed);

  DoubleValue simV;
  GlobalValue::GetValueByName("simTime", simV);
  g_simStopTime = simV.Get();
  StringValue outV;
  GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();
  fs::create_directories(outDir);
  fs::current_path(outDir);

  // E2 configuration
  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity",       DoubleValue(0.1));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte",                 BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr",                  BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2Periodicity",             DoubleValue(0.1));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp",                  StringValue("10.0.2.10"));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport",      BooleanValue(true));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport",    BooleanValue(true));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport",    BooleanValue(true));

  // PHY configuration — 28 GHz, 56 MHz BW
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq",          DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",           DoubleValue(56e6));

  // ----------------------------------------------------------------
  //  Channel model: UMa (cheaper than UMi Street Canyon)
  //  + BuildingsChannelConditionModel (wall still causes NLOS)
  // ----------------------------------------------------------------
  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  mmw->SetPathlossModelType("ns3::ThreeGppUmaPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::BuildingsChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();

  // ----------------------------------------------------------------
  //  Nodes: 1 gNB, 6 UEs, 1 remote host
  // ----------------------------------------------------------------
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(6);
  NodeContainer rh;  rh.Create(1);
  InternetStackHelper ip;
  ip.Install(ue);
  ip.Install(rh);

  // gNB position: (25, 25, 10)
  const Vector gnbPos(25, 25, 10);
  {
    MobilityHelper m;
    auto pos = CreateObject<ListPositionAllocator>();
    pos->Add(gnbPos);
    m.SetPositionAllocator(pos);
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    m.Install(gnb);
  }

  // ----------------------------------------------------------------
  //  Wall: X=[50,55], Y=[0,50], Z=[0,10]
  //  Anything crossing X=50..55 at Y<50 goes THROUGH the wall.
  //  All mobile UE paths cross at Y>=55 to go around the top.
  // ----------------------------------------------------------------
  Ptr<Building> wall = CreateObject<Building>();
  wall->SetBoundaries(Box(50.0, 55.0, 0.0, 50.0, 0.0, 10.0));
  wall->SetBuildingType(Building::Commercial);
  wall->SetExtWallsType(Building::ConcreteWithWindows);
  wall->SetNRoomsX(1);
  wall->SetNRoomsY(1);
  wall->SetNFloors(1);

  // ----------------------------------------------------------------
  //  UE Mobility — 6 diverse patterns, all wall-aware
  //
  //  UE0: Cyclic loop around the wall (NLOS transitions)
  //  UE1: Fast linear shuttle above the wall (cell-edge stress)
  //  UE2: Stationary near gNB (good signal reference)
  //  UE3: Stationary at cell edge, east side (poor signal reference)
  //  UE4: Slow walk on west side only (never crosses wall)
  //  UE5: Wide arc from near gNB to far east via wall top
  // ----------------------------------------------------------------
  MobilityHelper uem;
  uem.SetMobilityModel("ns3::WaypointMobilityModel");
  uem.Install(ue);

  // -- UE0: Cyclic around wall, 55s cycle --
  //    start(30,25) -> above wall(45,55) -> past wall(65,55) -> behind(70,25)
  //    -> far corner(95,90) -> return above(48,55) -> start
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(0)->GetObject<WaypointMobilityModel>();
    std::vector<Vector> pts = {
      Vector(30, 25, 1.5),   // start, near gNB
      Vector(45, 55, 1.5),   // move north above wall top (Y=55 > 50)
      Vector(65, 55, 1.5),   // east past wall at Y=55
      Vector(70, 25, 1.5),   // drop south behind wall (east side, X=70 > 55)
      Vector(95, 90, 1.5),   // far corner
      Vector(48, 55, 1.5),   // return west above wall (X=48 < 50, Y=55)
      Vector(30, 25, 1.5),   // back to start
    };
    std::vector<double> offsets = {0, 8, 14, 24, 34, 44, 55};
    AddCyclicWaypoints(mob, pts, offsets, 55.0, g_simStopTime);
  }

  // -- UE1: Fast linear shuttle at Y=55, X=20..140, 30s round trip --
  //    Always above the wall (Y=55 > 50). Simulates vehicular speed.
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(1)->GetObject<WaypointMobilityModel>();
    double t = 0.0;
    bool outward = false;
    while (t < g_simStopTime) {
      double x = outward ? 140.0 : 20.0;
      mob->AddWaypoint(Waypoint(Seconds(t), Vector(x, 55.0, 1.5)));
      t += 15.0;
      outward = !outward;
    }
  }

  // -- UE2: Stationary near gNB (good signal baseline) --
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(2)->GetObject<WaypointMobilityModel>();
    mob->AddWaypoint(Waypoint(Seconds(0.0), Vector(28, 30, 1.5)));
  }

  // -- UE3: Stationary at cell edge, east side (poor signal) --
  //    X=85, Y=55: east of wall, above wall top. Far from gNB.
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(3)->GetObject<WaypointMobilityModel>();
    mob->AddWaypoint(Waypoint(Seconds(0.0), Vector(85, 55, 1.5)));
  }

  // -- UE4: Slow walk on west side only, 40s cycle --
  //    Rectangle: (15,10) -> (15,45) -> (45,45) -> (45,10) -> back
  //    All X < 50, never touches wall.
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(4)->GetObject<WaypointMobilityModel>();
    std::vector<Vector> pts = {
      Vector(15, 10, 1.5),
      Vector(15, 45, 1.5),
      Vector(45, 45, 1.5),
      Vector(45, 10, 1.5),
      Vector(15, 10, 1.5),   // back to start
    };
    std::vector<double> offsets = {0, 10, 20, 30, 40};
    AddCyclicWaypoints(mob, pts, offsets, 40.0, g_simStopTime);
  }

  // -- UE5: Wide arc from near gNB to far east via wall top, 50s cycle --
  //    (35,30) -> north(35,55) -> east past wall(75,55) -> SE(75,80)
  //    -> return west above wall(35,55) -> south back(35,30)
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(5)->GetObject<WaypointMobilityModel>();
    std::vector<Vector> pts = {
      Vector(35, 30, 1.5),   // near gNB
      Vector(35, 55, 1.5),   // north above wall
      Vector(75, 55, 1.5),   // east past wall
      Vector(75, 80, 1.5),   // wander NE
      Vector(35, 55, 1.5),   // return west above wall
      Vector(35, 30, 1.5),   // back near gNB
    };
    std::vector<double> offsets = {0, 8, 18, 28, 38, 50};
    AddCyclicWaypoints(mob, pts, offsets, 50.0, g_simStopTime);
  }

  // Core network nodes (stationary)
  {
    MobilityHelper coreMob;
    coreMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> cp = CreateObject<ListPositionAllocator>();
    cp->Add(Vector(20, 25, 0));  // PGW
    cp->Add(Vector(20, 30, 0));  // SGW
    cp->Add(Vector(20, 20, 0));  // Remote host
    coreMob.SetPositionAllocator(cp);
    NodeContainer coreNodes;
    coreNodes.Add(pgw);
    coreNodes.Add(NodeList::GetNode(1));
    coreNodes.Add(rh.Get(0));
    coreMob.Install(coreNodes);
  }

  // Install buildings awareness on all radio nodes
  BuildingsHelper::Install(gnb);
  BuildingsHelper::Install(ue);

  // ----------------------------------------------------------------
  //  Radio devices & IP
  // ----------------------------------------------------------------
  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);

  Ipv4StaticRoutingHelper srt;
  for (uint32_t u = 0; u < ue.GetN(); ++u)
    srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>())
       ->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);

  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  // Remote host backhaul
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  auto d = p2p.Install(pgw, rh.Get(0));
  Ipv4AddressHelper a;
  a.SetBase("10.0.0.0", "255.0.0.0");
  a.Assign(d);
  Ipv4StaticRoutingHelper srh;
  srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())
     ->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // ----------------------------------------------------------------
  //  Traffic: 50 Mbps UDP per UE from remote host
  // ----------------------------------------------------------------
  const uint16_t basePort = 4000;
  ApplicationContainer allSinks;
  for (uint32_t i = 0; i < ue.GetN(); ++i) {
    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
    allSinks.Add(sink.Install(ue.Get(i)));

    OnOffHelper source("ns3::UdpSocketFactory",
                       InetSocketAddress(ueIf.GetAddress(i), basePort + i));
    source.SetAttribute("OnTime",  StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
    source.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));
    source.SetAttribute("DataRate", DataRateValue(DataRate("50Mbps")));
    source.Install(rh.Get(0)).Start(Seconds(1.0 + (i * 0.1)));
  }
  allSinks.Start(Seconds(0.5));

  // Ping for latency measurement (to UE0)
  V4PingHelper ping(ueIf.GetAddress(0));
  ping.SetAttribute("Interval", TimeValue(Seconds(0.1)));
  ping.SetAttribute("Size", UintegerValue(56));
  ping.SetAttribute("Verbose", BooleanValue(false));
  ping.Install(rh.Get(0)).Start(Seconds(0.5));
  Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt",
                                MakeCallback(&PingRttCallback));

  mmw->EnableTraces();  // Could maybe be disabled: E2 reports use their own stats calculators for the AI system, 
  // but the AI system still uses some of the traces for feature extraction (e.g. MCS)
  // The performance hit of leaving it enabled is ~8% according to my testing


  // ----------------------------------------------------------------
  //  Schedule periodic callbacks
  // ----------------------------------------------------------------
  const double covRadius = 100.0;
  Simulator::Schedule(Seconds(1.0), &SampleAll,
                      std::ref(ue), std::ref(ueDevs), gnb.Get(0),
                      covRadius, allSinks, 0.5);

  // Progress printer every 1 sim-second (+ wall-clock fallback)
  Simulator::Schedule(Seconds(0.0), &PrintProgress);

  // Chaos events start after 30s warm-up
  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
  Simulator::Schedule(Seconds(30.0), &ScheduleRandomEvent, rng, ue, gnb.Get(0), rh);

  NS_LOG_UNCOND("=== NOVEL EVALUATION SCENARIO ===");
  NS_LOG_UNCOND("  UEs: " << ue.GetN() << ", Duration: " << g_simStopTime << "s, Seed: " << rngSeed);
  NS_LOG_UNCOND("  Channel: ThreeGppUma + BuildingsChannelCondition");
  NS_LOG_UNCOND("  Wall: X=[50,55] Y=[0,50]  |  gNB: (25,25,10)");
  NS_LOG_UNCOND("  Chaos: blockage + traffic spike (no MCS drop)");
  NS_LOG_UNCOND("  Warm-up: 30s before first chaos event");
  NS_LOG_UNCOND("=================================");

  Simulator::Stop(Seconds(g_simStopTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

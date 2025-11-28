
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
#include "ns3/waypoint-mobility-model.h"
#include "ns3/buildings-module.h"
#include "ns3/buildings-helper.h" // Add this

#include "ns3/mmwave-component-carrier-enb.h"  // Add this
#include "ns3/mmwave-flex-tti-mac-scheduler.h" // Add this

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <vector>

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_1gNB_1UE");

// ---------------- Runtime flags ----------------
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(100.0), MakeDoubleChecker<double>(1.0, 3600.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

// (kept from your file)
static GlobalValue q_useSemaphores ("useSemaphores","If true, enables the use of semaphores for external environment control",
                                    BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_controlFileName ("controlFileName","The path to the control file (can be absolute)",
                                      StringValue(""), MakeStringChecker());
static GlobalValue g_e2lteEnabled ("e2lteEnabled","If true, send LTE E2 reports",
                                   BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2nrEnabled ("e2nrEnabled","If true, send NR E2 reports",
                                  BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2du ("e2du","If true, send DU reports",
                           BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuUp ("e2cuUp","If true, send CU-UP reports",
                             BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_e2cuCp ("e2cuCp","If true, send CU-CP reports",
                             BooleanValue(true), MakeBooleanChecker());
static GlobalValue g_indicationPeriodicity ("indicationPeriodicity","E2 Indication Periodicity (s)", 
                                            DoubleValue(0.1), MakeDoubleChecker<double>(0.01, 2.0));
static GlobalValue g_e2TermIp ("e2TermIp","RIC E2 termination IP",
                               StringValue("10.0.2.10"), MakeStringChecker());
static GlobalValue g_enableE2FileLogging ("enableE2FileLogging","Offline file logging instead of connecting to RIC",
                                          BooleanValue(false), MakeBooleanChecker());
static GlobalValue g_reducedPmValues ("reducedPmValues", "If true, use a subset of the pm containers",
                                      BooleanValue(false), MakeBooleanChecker());



 
// ---------------- Timeseries sampler (drop-in)// Global variables
struct GlobalState {
  double lastT = 0.0;
  uint64_t lastBytes = 0;
  double ewma = 0.0;
  bool seenPing = false;
  double lastPingMs = 0.0;
} gS;

AnimationInterface *g_anim = nullptr; // Global pointer for NetAnim updates

static void PingRttCallback(Time rtt) {
  gS.lastPingMs = rtt.GetMilliSeconds();
  gS.seenPing   = true;
}

static void SampleAll(const NodeContainer &ueNodes,
                      const NetDeviceContainer &ueDevs,
                      Ptr<Node> gnbNode,
                      double covRadius,
                      Ptr<PacketSink> sink0,
                      double periodSec)
{
  static std::ofstream f;
  static bool headerDone = false;

  if (!headerDone) {
    f.open("sim_timeseries.csv", std::ios::out | std::ios::trunc);
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
    f << ",throughput_ue0_mbps"
      << ",throughput_ue0_ewma"
      << ",ping_ms"
      << ",mcs_dl"           // Add MCS column
      << ",mcs_ul"           // Add UL MCS column
      << ",fixed_mcs_dl"     // Add fixed MCS flag
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

  double mbps = 0.0;
  if (sink0) {
    uint64_t bytes = sink0->GetTotalRx();
    double dt = now - gS.lastT;
    if (gS.lastT > 0.0 && dt > 0.0) {
      mbps = 8.0 * (bytes - gS.lastBytes) / dt / 1e6;
    }
    gS.lastBytes = bytes;
    gS.lastT = now;
  }

  const double tau = 1.0; // EWMA time constant (s)
  const double alpha = 1.0 - std::exp(-(periodSec / tau));
  gS.ewma = alpha * mbps + (1.0 - alpha) * gS.ewma;

  double pingMs = gS.seenPing ? gS.lastPingMs : 0.0;



  // Get MCS values from scheduler
  uint8_t mcsDl = 255;  // 255 = adaptive/not fixed
  uint8_t mcsUl = 255;
  bool fixedMcsDl = false;
  
  Ptr<mmwave::MmWaveEnbNetDevice> enbDev = gnbNode->GetDevice(0)->GetObject<mmwave::MmWaveEnbNetDevice>();
  if (enbDev) {
    // Access scheduler through component carrier
    std::map<uint8_t, Ptr<mmwave::MmWaveComponentCarrier>> ccMap = enbDev->GetCcMap();
    if (!ccMap.empty()) {
      Ptr<mmwave::MmWaveComponentCarrierEnb> cc = 
          DynamicCast<mmwave::MmWaveComponentCarrierEnb>(ccMap.at(0));
      if (cc) {
        Ptr<mmwave::MmWaveMacScheduler> sched = cc->GetMacScheduler();
        if (sched) {
          Ptr<mmwave::MmWaveFlexTtiMacScheduler> flexSched = 
              DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(sched);
          if (flexSched) {
            mcsDl = flexSched->GetCurrentMcsDl();
            mcsUl = flexSched->GetCurrentMcsUl();
            fixedMcsDl = flexSched->IsFixedMcsDl();
          }
        }
      }
    }
  }

  f << "," << mbps
    << "," << gS.ewma
    << "," << pingMs
    << "," << static_cast<int>(mcsDl)    // Add MCS DL value
    << "," << static_cast<int>(mcsUl)    // Add MCS UL value
    << "," << (fixedMcsDl ? 1 : 0)       // Add fixed MCS flag
    << "\n";
  f.flush();

  // Update NetAnim description with throughput
  if (g_anim) {
      std::ostringstream oss;
      oss << "UE0 (" << std::fixed << std::setprecision(1) << mbps << " Mbps)";
      g_anim->UpdateNodeDescription(ueNodes.Get(0), oss.str());
  }

  Simulator::Schedule(Seconds(periodSec), &SampleAll,
                      ueNodes, ueDevs, gnbNode, covRadius, sink0, periodSec);
}

// ---------------- (Optional) simple position dumper you had ----------------
static void SamplePositions(NodeContainer ueNodes,
                            NetDeviceContainer ueDevs,
                            Ptr<Node> gnbNode,
                            double periodSec)
{
  static std::ofstream f("ue_positions.csv", std::ios::out | std::ios::trunc);
  static bool header = false;
  if (!header) {
    f << "time_s,ue_index,imsi,x,y,z,dist_to_gnb_m\n";
    header = true;
  }

  double t = Simulator::Now().GetSeconds();
  Vector gp = gnbNode->GetObject<MobilityModel>()->GetPosition();

  for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
    Vector p = ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
    double dx = p.x - gp.x, dy = p.y - gp.y, dz = p.z - gp.z;
    double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    uint64_t imsi = ueDevs.Get(i)->GetObject<MmWaveUeNetDevice>()->GetImsi();
    f << t << "," << i << "," << imsi << "," << p.x << "," << p.y << "," << p.z << "," << dist << "\n";
  }
  f.flush();
  Simulator::Schedule(Seconds(periodSec), &SamplePositions, ueNodes, ueDevs, gnbNode, periodSec);
}

// ---------------- Dynamic MCS Logic ----------------
// Sets the MCS value for the scheduler leaves the mcs as the chosen value for 10 seconds
// Unless the value have been changed then leave it an extra 5 seconds
// This is to prevent the scheduler from changing the MCS value if the AI just changed it
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

  // We need to cast to MmWaveFlexTtiMacScheduler to access attributes
  Ptr<mmwave::MmWaveFlexTtiMacScheduler> flexSched = DynamicCast<mmwave::MmWaveFlexTtiMacScheduler>(sched);
  if (flexSched) {
    if (mcs >= 0) {
      flexSched->SetAttribute("FixedMcsDl", BooleanValue(true));
      flexSched->SetAttribute("McsDefaultDl", UintegerValue(mcs));
      flexSched->SetAttribute("FixedMcsUl", BooleanValue(true));
      flexSched->SetAttribute("McsDefaultUl", UintegerValue(mcs));
      NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] Setting Fixed MCS to " << mcs);
    } else {
      flexSched->SetAttribute("FixedMcsDl", BooleanValue(false));
      flexSched->SetAttribute("FixedMcsUl", BooleanValue(false));
      NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] Restoring Adaptive MCS");
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

static void ScheduleNextMcsEvent(Ptr<Node> gnb, bool nextIsLow)
{
  if (nextIsLow) {
    // --- Low Phase ---
    // Target: Random MCS between 4 and 9 (very low)
    int targetMcs = 4 + (rand() % 6); 
    ChangeMcs(gnb, targetMcs);
    
    // Duration: 10 seconds
    double duration = 10.0;
    
    Simulator::Schedule(Seconds(duration), [gnb, targetMcs]() {
      // Check for AI intervention
      int currentMcs = GetCurrentMcs(gnb);
      if (currentMcs != -1 && currentMcs != targetMcs) {
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] AI intervention detected (MCS=" << currentMcs << " != " << targetMcs << "). Extending window by 5s.");
        Simulator::Schedule(Seconds(5.0), [gnb]() {
          ScheduleNextMcsEvent(gnb, false); // Go to Random Phase
        });
      } else {
        ScheduleNextMcsEvent(gnb, false); // Go to Random Phase
      }
    });
  } else {
    // --- Random Phase ---
    // Target: Random MCS between 1 and 28
    int targetMcs = 1 + (rand() % 28);
    ChangeMcs(gnb, targetMcs);
    
    // Duration: 10 seconds
    double duration = 10.0;
    
    Simulator::Schedule(Seconds(duration), [gnb, targetMcs]() {
      // Check for AI intervention
      int currentMcs = GetCurrentMcs(gnb);
      if (currentMcs != -1 && currentMcs != targetMcs) {
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] AI intervention detected (MCS=" << currentMcs << " != " << targetMcs << "). Extending window by 5s.");
        Simulator::Schedule(Seconds(5.0), [gnb]() {
          ScheduleNextMcsEvent(gnb, true); // Go to Low Phase
        });
      } else {
        ScheduleNextMcsEvent(gnb, true); // Go to Low Phase
      }
    });
  }
}

// ---------------- main ----------------
int main (int argc, char** argv)
{
  // Enable logging for E2 components (like scenario-zero.cc)
  // LogComponentEnableAll (LOG_PREFIX_ALL);
  // LogComponentEnable ("RicControlMessage", LOG_LEVEL_ALL);
  // LogComponentEnable ("E2Termination", LOG_LEVEL_LOGIC);

  CommandLine cmd;
  int rngSeed = 0;
  cmd.AddValue("rngSeed", "Seed for random number generator (default 0 = random)", rngSeed);
  cmd.Parse(argc, argv);

  // Initialize random seed
  if (rngSeed == 0) {
    srand(time(NULL));
    NS_LOG_UNCOND("RNG Seed: Random (time-based)");
  } else {
    srand(rngSeed);
    NS_LOG_UNCOND("RNG Seed: Fixed (" << rngSeed << ")");
  }

  // Read flags
  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simTime = simV.Get();
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

  // Clear control files at startup to ensure scenario starts with defaults
  // This allows configs to be applied during runtime without affecting initial state
  if (!controlFilename.empty())
  {
    // Extract directory from control filename (default: /tmp/ns3-control)
    std::string controlDir = "/tmp/ns3-control";
    size_t lastSlash = controlFilename.find_last_of('/');
    if (lastSlash != std::string::npos)
    {
      controlDir = controlFilename.substr(0, lastSlash);
    }
    
    // List of control files to clear
    std::vector<std::string> controlFiles = {
      controlDir + "/qos_actions.csv",
      controlDir + "/ts_actions_for_ns3.csv",
      controlDir + "/es_actions_for_ns3.csv",
      controlDir + "/enb_txpower_actions.csv",
      controlDir + "/ue_txpower_actions.csv",
      controlDir + "/cbr_actions.csv",
      controlDir + "/prb_cap_actions.csv"
    };
    
    NS_LOG_UNCOND("Clearing control files at startup to ensure default settings...");
    for (const auto& file : controlFiles)
    {
      std::ofstream clearFile(file, std::ios::trunc);
      if (clearFile.is_open())
      {
        clearFile.close();
        NS_LOG_UNCOND("Cleared control file: " << file);
      }
      else
      {
        // File might not exist yet, that's okay
        NS_LOG_DEBUG("Control file does not exist (will be created when needed): " << file);
      }
    }
    NS_LOG_UNCOND("Control files cleared. Scenario will start with default settings.");
  }

  NS_LOG_UNCOND ("e2lteEnabled " << e2lteEnabled << " e2nrEnabled " << e2nrEnabled << " e2du "
                                 << e2du << " e2cuCp " << e2cuCp << " e2cuUp " << e2cuUp
                                 << " controlFilename " << controlFilename
                                 << " useSemaphores " << useSemaphores
                                 << " indicationPeriodicity " << indicationPeriodicity
                                 << " reducedPmValues " << reducedPmValues);



//-----------------------E2 CONFIGURATION----------------------------
  // Apply E2 config like ScenarioZero
  // Note: UseSemaphores and ControlFileName are only available for LteEnbNetDevice,
  // not for MmWaveEnbNetDevice. Control file reading is handled by LteEnbNetDevice
  // in dual-connectivity scenarios.

  // E2 periodicity on the device
  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity",
                     DoubleValue(indicationPeriodicity));

  // Helper-level E2 config
  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte",
                     BooleanValue(e2lteEnabled));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr",
                     BooleanValue(e2nrEnabled));
  Config::SetDefault("ns3::MmWaveHelper::E2Periodicity",
                     DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp",
                     StringValue(e2TermIp));

  // Device-level E2 report switches
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport",
                     BooleanValue(e2du));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport",
                     BooleanValue(e2cuUp));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport",
                     BooleanValue(e2cuCp));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableE2FileLogging",
                     BooleanValue(enableE2FileLogging));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::ReducedPmValues",
                     BooleanValue(reducedPmValues));

// HARQ on/off (maps to m_harqOn)
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled",
                   BooleanValue(true));

// Start with AMC (no fixed MCS)
// Start with Fixed MCS 28 (High Throughput)
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::FixedMcsDl",
                   BooleanValue(true));
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::FixedMcsUl",
                   BooleanValue(true));

// Default values that DoSchedSetMcs will override anyway
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::McsDefaultDl",
                   UintegerValue(28));
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::McsDefaultUl",
                   UintegerValue(28));

// Optional: DL-only / UL-only for debugging
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::DlSchedOnly",
                   BooleanValue(false));
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::UlSchedOnly",
                   BooleanValue(false));

// Optional: static TTI
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::FixedTti",
                   BooleanValue(false));
Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::SymPerSlot",
                   UintegerValue(6));

  // Note: The following attributes do NOT exist in MmWaveFlexTtiMacScheduler:
  // - Policy, DlMcsMode, DlMcsValue, UlMcsMode, UlMcsValue, SymbolsPerUe
  // Available attributes are: HarqEnabled, FixedMcsDl, McsDefaultDl, FixedMcsUl,
  // McsDefaultUl, DlSchedOnly, UlSchedOnly, FixedTti, SymPerSlot, CqiTimerThreshold
// --------------------------------------------------------------------


//-------------------------------------------------------------
  // RF/system defaults (optional but handy)
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(56e6));
  Config::SetDefault("ns3::MmWaveEnbPhy::TxPower",          DoubleValue(10.0));
  Config::SetDefault("ns3::MmWaveUePhy::NoiseFigure",       DoubleValue(7.0));

  // Output dir
  fs::create_directories(outDir);
  fs::current_path(outDir);

  // Helpers
  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);

// ------------------- BUILDING  SIZE ---------------------------------
  // Add a building in the way the signal to the UE
  // Ptr<Building> b = CreateObject<Building> ();
  // b->SetBoundaries (Box (80.0, 90.0,     
  //                        0.0, 100.0,     
  //                        0.0, 20.0));    
  // b->SetBuildingType (Building::Residential);
  // b->SetExtWallsType (Building::ConcreteWithWindows); 

// ------------------- BUILDING  PATHLOSS -----------------------------
  // mmw->SetPathlossModelType("ns3::HybridBuildingsPropagationLossModel");
  // mmw->SetChannelConditionModelType("ns3::BuildingsChannelConditionModel");
// --------------------------------------------------------------------

  mmw->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();

  // Nodes
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(1);
  NodeContainer rh;  rh.Create(1);
  NodeContainer buildingNode; buildingNode.Create(1); // Dummy node for building visualization

  // Internet stacks
  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  // Mobility: fixed gNB; UE does RandomWalk2d
  const Vector gnbPos = Vector(25,25,10);
  {
    MobilityHelper m;
    auto enbPos = CreateObject<ListPositionAllocator>();
    enbPos->Add(gnbPos);
    m.SetPositionAllocator(enbPos);
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    m.Install(gnb);

    // ------------------- BUILDING POSITION -----------------------------    
    // Install mobility for building node (static at center of building)
    // MobilityHelper buildingMobility;
    // auto buildingPos = CreateObject<ListPositionAllocator>();
    // buildingPos->Add(Vector(85.0, 50.0, 0.0)); // Center of X=[80,90], Y=[0,100]
    // buildingMobility.SetPositionAllocator(buildingPos);
    // buildingMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    // buildingMobility.Install(buildingNode);
    // --------------------------------------------------------------------
  }

  // ------------------- UE MOBILITY -----------------------------
  MobilityHelper uem;
    uem.SetMobilityModel("ns3::WaypointMobilityModel");
    uem.Install(ue);

    Ptr<WaypointMobilityModel> ueMobility = ue.Get(0)->GetObject<WaypointMobilityModel>();


    double t_cycle = 0.0;
    double cycle_duration = 50.0;

    Vector p_start(30, 25, 1.5);    // Close to gNB (LOS)
    Vector p_wall_front(70, 25, 1.5); // Just before building
    Vector p_wall_back(95, 25, 1.5);  // Behind building (NLOS/Penetration)
    Vector p_far_corner(95, 110, 1.5); // Far corner (Shadowed)
    Vector p_clear(50, 110, 1.5);      // Clear of building (LOS)

    // 0s: Start (Initial position)
    ueMobility->AddWaypoint(Waypoint(Seconds(0.0), p_start));

    while (t_cycle < simTime) {
        // Note: The start point for the current cycle is already added 
        // (either by initialization or by the end of the previous cycle).
        
        // 10s: Approach wall
        if (t_cycle + 10.0 > simTime) break;
        ueMobility->AddWaypoint(Waypoint(Seconds(t_cycle + 10.0), p_wall_front));

        // 20s: Go through/behind wall -> Drop in throughput
        if (t_cycle + 20.0 > simTime) break;
        ueMobility->AddWaypoint(Waypoint(Seconds(t_cycle + 20.0), p_wall_back));

        // 30s: Move along back -> Low throughput
        if (t_cycle + 30.0 > simTime) break;
        ueMobility->AddWaypoint(Waypoint(Seconds(t_cycle + 30.0), p_far_corner));

        // 40s: Move clear -> Recovery
        if (t_cycle + 40.0 > simTime) break;
        ueMobility->AddWaypoint(Waypoint(Seconds(t_cycle + 40.0), p_clear));

        // 50s: Return to start (closes the loop and sets start for next cycle)
        if (t_cycle + 50.0 <= simTime) {
             ueMobility->AddWaypoint(Waypoint(Seconds(t_cycle + 50.0), p_start));
        }
        
        t_cycle += cycle_duration;
    }

  // Core + RH “fixed” positions (optional)
  {
    Ptr<Node> sgw = NodeList::GetNode(1);
    NodeContainer stationaryCoreNodes; stationaryCoreNodes.Add(pgw); stationaryCoreNodes.Add(sgw); stationaryCoreNodes.Add(rh.Get(0));
    MobilityHelper coreMobility; coreMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> corePositions = CreateObject<ListPositionAllocator>();
    corePositions->Add(Vector(20.0,25.0,0.0));
    corePositions->Add(Vector(20.0,30.0,0.0));
    corePositions->Add(Vector(20.0,20.0,0.0));
    coreMobility.SetPositionAllocator(corePositions);
    coreMobility.Install(stationaryCoreNodes);
    coreMobility.SetPositionAllocator(corePositions);
    coreMobility.Install(stationaryCoreNodes);
  }

  // Install BuildingsHelper to enable building-aware mobility/channel
  BuildingsHelper::Install(gnb);
  BuildingsHelper::Install(ue);

  // Devices
  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);

  // (Optional) your simple per-UE position CSV
  SamplePositions(ue, ueDevs, gnb.Get(0), 0.5);

  // EPC addressing
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ue.GetN(); ++u) {
    Ptr<Ipv4StaticRouting> r = srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>());
    r->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  }

  // Attach UE
  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  // Backhaul: PGW <-> RemoteHost
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer d = p2p.Install(pgw, rh.Get(0));
  Ipv4AddressHelper a; a.SetBase("10.0.0.0","255.0.0.0");
  a.Assign(d);
  Ipv4StaticRoutingHelper srh;
  srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())
     ->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // ------------- Traffic for throughput & ping  -------------
  const uint16_t cbrPort = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), cbrPort));
  ApplicationContainer sinkApps = sink.Install(ue.Get(0));
  sinkApps.Start(Seconds(0.2));
  Ptr<PacketSink> sinkApp = DynamicCast<PacketSink>(sinkApps.Get(0));

  OnOffHelper cbr("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(0), cbrPort));
  cbr.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  cbr.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  cbr.SetAttribute("DataRate", StringValue("50Mbps"));
  cbr.SetAttribute("PacketSize", UintegerValue(1200));
  cbr.Install(rh.Get(0)).Start(Seconds(0.35));

  V4PingHelper ping(ueIf.GetAddress(0));
  ping.SetAttribute("Verbose", BooleanValue(false));
  ping.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  ApplicationContainer p = ping.Install(rh.Get(0));
  p.Start(Seconds(0.6));
  Ptr<V4Ping> pingApp = DynamicCast<V4Ping>(p.Get(0));
  pingApp->TraceConnectWithoutContext("Rtt", MakeCallback(&PingRttCallback));

  // mmWave traces
  mmw->EnableTraces();

  // Start the unified sampler (every 0.1 s) — adjust radius as you like
  const double covRadius = 100.0;
  Simulator::Schedule(Seconds(0.1), &SampleAll,
                      std::ref(ue), std::ref(ueDevs), gnb.Get(0),
                      covRadius, sinkApp, 0.1);

  // (Optional) quick labels + NetAnim
  {
    std::ofstream ues("ues.txt"), enbs("enbs.txt");
    Ptr<MobilityModel> mm = ue.Get(0)->GetObject<MobilityModel>();
    Vector up = mm->GetPosition();
    ues  << "UE IMSI " << ueDevs.Get(0)->GetObject<MmWaveUeNetDevice>()->GetImsi()
         << " " << up.x << " " << up.y << "\n";
    Ptr<MobilityModel> em = gnb.Get(0)->GetObject<MobilityModel>();
    Vector ep = em->GetPosition();
    enbs << "gNB CellId " << gnbDevs.Get(0)->GetObject<MmWaveEnbNetDevice>()->GetCellId()
         << " " << ep.x << " " << ep.y << "\n";
  }


  // Timestamped NetAnim file
  std::time_t t = std::time(nullptr);
  std::tm tm = *std::localtime(&t);
  char time_buffer[80];
  std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d_%H-%M-%S", &tm);
  std::string filename = std::string("NetAnimFile_") + time_buffer + ".xml";
  AnimationInterface anim(filename.c_str());
  g_anim = &anim; // Assign global pointer

  anim.SetMobilityPollInterval(Seconds(1)); // Sample movement every 1 second
  anim.SkipPacketTracing(); // Disable packet tracing to reduce file size

  // // Configure building visualization
  // uint32_t buildingImgId = anim.AddResource("/home/hybrid/proj/ns-3-mmwave-oran/scratch/building.png");
  // anim.UpdateNodeImage(buildingNode.Get(0)->GetId(), buildingImgId);
  // anim.UpdateNodeDescription(buildingNode.Get(0), "Building");
  // anim.UpdateNodeSize(buildingNode.Get(0), 10.0, 100.0); // Width=10, Height=100
  
  // Start the alternating MCS loop (Low -> Random -> Low...)
  // First 5 seconds are Fixed MCS 28 (set by default)
  Simulator::Schedule(Seconds(5.0), &ScheduleNextMcsEvent, gnb.Get(0), true);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <vector>

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_1gNB_1UE_v3");

//simulation
static const double sim_duration = 1800.0;

// ---------------- Runtime flags ----------------
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(sim_duration), MakeDoubleChecker<double>(1.0, 3600.0)); 
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
  std::vector<uint64_t> lastBytes;
  std::vector<double> ewma;
  bool seenPing = false;
  double lastPingMs = 0.0;
} gS;

enum RandomEventType {
    EVENT_NONE,
    EVENT_BLOCKAGE,
    EVENT_TRAFFIC_SPIKE
};

struct RandomEventState {
    RandomEventType activeEvent = EVENT_NONE;
    uint32_t affectedIdx = 0;
    double originalValue = 0.0;
    Ptr<mmwave::MmWaveUePhy> affectedPhy = nullptr;
    Ptr<OnOffApplication> affectedApp = nullptr;
    DataRate originalRate;
} g_eventState;

// Event tracking globals
static std::string g_activeMcsEvent = "None";
static bool g_activeBlockage = false;
static uint64_t g_blockageImsi = 0;
static bool g_activeTrafficSpike = false;
static uint32_t g_trafficSpikeRhIdx = 0;

AnimationInterface *g_anim = nullptr; // Global pointer for NetAnim updates

// ================================================================
//  Progress printer — every 1 sim-second AND at least every 10 real seconds
// ================================================================
static auto g_realStart = std::chrono::steady_clock::now();
static double g_lastPrintReal = 0.0;

static void PrintProgress() {
  auto elapsed = std::chrono::steady_clock::now() - g_realStart;
  double realSec = std::chrono::duration<double>(elapsed).count();
  double simSec = Simulator::Now().GetSeconds();

  DoubleValue simV;
  GlobalValue::GetValueByName("simTime", simV);
  double stopTime = simV.Get();

  double ratio = (realSec > 0.0) ? (simSec / realSec) : 0.0;
  double pct = (stopTime > 0.0) ? (simSec / stopTime * 100.0) : 0.0;

  // Print if ≥10 real seconds since last print, or always (1 sim-sec cadence)
  if ((realSec - g_lastPrintReal) >= 10.0 || simSec <= 1.0 ||
      static_cast<int>(simSec) % 1 == 0) {
    std::cout << "  [SIM] t=" << std::fixed << std::setprecision(1)
      << simSec << "/" << stopTime << "s  "
      << std::setprecision(0) << pct << "%"
      << "  ('real' time: " << (int)realSec << "s, ratio: "
      << std::setprecision(3) << ratio << "x)" << std::endl;
    g_lastPrintReal = realSec;
  }

  // Re-schedule every 1 sim-second
  Simulator::Schedule(Seconds(1.0), &PrintProgress);
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
      << ",mcs_dl"           // Add MCS column
      << ",mcs_ul"           // Add UL MCS column
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

  // Resize global state vectors if needed
  if (gS.lastBytes.size() < sinkApps.GetN()) {
      gS.lastBytes.resize(sinkApps.GetN(), 0);
      gS.ewma.resize(sinkApps.GetN(), 0.0);
  }

  double dt = now - gS.lastT;
  const double tau = 1.0; // EWMA time constant (s)
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

  f << "," << pingMs
    << "," << static_cast<int>(mcsDl)    // Add MCS DL value
    << "," << static_cast<int>(mcsUl)    // Add MCS UL value
    << "," << g_activeMcsEvent;

  if (g_activeBlockage) {
      f << ",IMSI_" << g_blockageImsi;
  } else {
      f << ",None";
  }

  if (g_activeTrafficSpike) {
      f << ",RH_" << g_trafficSpikeRhIdx;
  } else {
      f << ",None";
  }

  f << "\n";
  f.flush();

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
      NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] Setting Fixed MCS to " << mcs);
      std::cerr << "  → MCS change applied: Fixed MCS=" << mcs << " (DL and UL)" << std::endl;
    } else {
      flexSched->SetAttribute("FixedMcsDl", BooleanValue(false));
      flexSched->SetAttribute("FixedMcsUl", BooleanValue(false));
      NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [Scenario] Restoring Adaptive MCS");
      std::cerr << "  → MCS change applied: Adaptive MCS restored (DL and UL)" << std::endl;
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
    int targetMcs = 4 + (rand() % 6); 
    ChangeMcs(gnb, targetMcs);
    g_activeMcsEvent = "Low";
    double duration = 10.0;
    std::cerr << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║  [EVENT] MCS DEGRADATION TRIGGERED                        ║\n"
              << "╠════════════════════════════════════════════════════════════╣\n"
              << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
              << "║  Action: Setting LOW MCS (4-9 range)                      ║\n"
              << "║  Target MCS: " << std::setw(42) << targetMcs << " ║\n"
              << "║  Duration: " << std::setw(45) << duration << "s ║\n"
              << "║  Impact: System performance will degrade                  ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] MCS DEGRADATION - Setting LOW MCS=" << targetMcs << " for " << duration << "s");
    Simulator::Schedule(Seconds(duration), [gnb, targetMcs]() {
      int currentMcs = GetCurrentMcs(gnb);
      if (currentMcs != -1 && currentMcs != targetMcs) {
        std::cerr << "\n"
                  << "╔════════════════════════════════════════════════════════════╗\n"
                  << "║  [EVENT] AI INTERVENTION DETECTED                         ║\n"
                  << "╠════════════════════════════════════════════════════════════╣\n"
                  << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                  << "║  Expected MCS: " << std::setw(40) << targetMcs << " ║\n"
                  << "║  Actual MCS: " << std::setw(43) << currentMcs << " ║\n"
                  << "║  Action: Extending event window by 5s                    ║\n"
                  << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] AI intervention detected (MCS=" << currentMcs << " != " << targetMcs << "). Extending window by 5s.");
        Simulator::Schedule(Seconds(5.0), [gnb]() { ScheduleNextMcsEvent(gnb, false); });
      } else {
        ScheduleNextMcsEvent(gnb, false);
      }
    });
  } else {
    int targetMcs = 1 + (rand() % 28);
    ChangeMcs(gnb, targetMcs);
    g_activeMcsEvent = "Random";
    double duration = 10.0;
    std::cerr << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║  [EVENT] MCS RANDOMIZATION TRIGGERED                      ║\n"
              << "╠════════════════════════════════════════════════════════════╣\n"
              << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
              << "║  Action: Setting RANDOM MCS (1-28 range)                  ║\n"
              << "║  Target MCS: " << std::setw(42) << targetMcs << " ║\n"
              << "║  Duration: " << std::setw(45) << duration << "s ║\n"
              << "║  Impact: Unpredictable system performance                 ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] MCS RANDOMIZATION - Setting RANDOM MCS=" << targetMcs << " for " << duration << "s");
    Simulator::Schedule(Seconds(duration), [gnb, targetMcs]() {
      int currentMcs = GetCurrentMcs(gnb);
      if (currentMcs != -1 && currentMcs != targetMcs) {
        std::cerr << "\n"
                  << "╔════════════════════════════════════════════════════════════╗\n"
                  << "║  [EVENT] AI INTERVENTION DETECTED                         ║\n"
                  << "╠════════════════════════════════════════════════════════════╣\n"
                  << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                  << "║  Expected MCS: " << std::setw(40) << targetMcs << " ║\n"
                  << "║  Actual MCS: " << std::setw(43) << currentMcs << " ║\n"
                  << "║  Action: Extending event window by 5s                    ║\n"
                  << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] AI intervention detected (MCS=" << currentMcs << " != " << targetMcs << "). Extending window by 5s.");
        Simulator::Schedule(Seconds(5.0), [gnb]() { ScheduleNextMcsEvent(gnb, true); });
      } else {
        ScheduleNextMcsEvent(gnb, true);
      }
    });
  }
}

// ---------------- NEW RANDOM EVENTS ----------------

static void ScheduleNextRandomEvent(NodeContainer ues, NodeContainer remoteHosts, Ptr<Node> gnb);

static void RestoreRandomEvent(NodeContainer ues, NodeContainer remoteHosts, Ptr<Node> gnb)
{
    if (g_eventState.activeEvent == EVENT_BLOCKAGE && g_eventState.affectedPhy) {
        g_eventState.affectedPhy->SetNoiseFigure(g_eventState.originalValue);
        g_activeBlockage = false;
        
        uint64_t imsi = g_blockageImsi;
        
        // --- NEW: Clean stdout one-liner ---
        std::cout << Simulator::Now().GetSeconds() << "s: [STDOUT] Blockage degradation ended on UE " << imsi << std::endl;
        
        std::cerr << "\n"
                  << "╔════════════════════════════════════════════════════════════╗\n"
                  << "║  [EVENT] RANDOM BLOCKAGE ENDED                           ║\n"
                  << "╠════════════════════════════════════════════════════════════╣\n"
                  << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                  << "║  Affected UE: " << std::setw(42) << imsi << " ║\n"
                  << "║  Noise Figure restored to: " << std::setprecision(1) << std::setw(30) << g_eventState.originalValue << " dB ║\n"
                  << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] Random Blockage ended for UE " << imsi << " (NF restored to " << g_eventState.originalValue << "dB)");
        
        g_eventState.affectedPhy = nullptr;
        g_eventState.activeEvent = EVENT_NONE;
        
    } else if (g_eventState.activeEvent == EVENT_TRAFFIC_SPIKE && g_eventState.affectedApp) {
        g_eventState.affectedApp->SetAttribute("DataRate", DataRateValue(g_eventState.originalRate));
        g_activeTrafficSpike = false;
        
        // --- NEW: Clean stdout one-liner ---
        std::cout << Simulator::Now().GetSeconds() << "s: [STDOUT] Traffic spike degradation ended on RH " << g_eventState.affectedIdx << std::endl;
        
        std::cerr << "\n"
                  << "╔════════════════════════════════════════════════════════════╗\n"
                  << "║  [EVENT] TRAFFIC SPIKE ENDED                              ║\n"
                  << "╠════════════════════════════════════════════════════════════╣\n"
                  << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                  << "║  Remote Host Index: " << std::setw(38) << g_eventState.affectedIdx << " ║\n"
                  << "║  Data Rate restored to: " << std::setw(35) << "50 Mbps ║\n"
                  << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] Traffic Spike ended for RH " << g_eventState.affectedIdx << " (restored to 50Mbps)");
        
        g_eventState.affectedApp = nullptr;
        g_eventState.activeEvent = EVENT_NONE;
    }

    // Schedule the NEXT event after a cooldown of 5 to 15 seconds
    double cooldown = 5.0 + (rand() % 11); // 5 to 15 seconds
    Simulator::Schedule(Seconds(cooldown), &ScheduleNextRandomEvent, ues, remoteHosts, gnb);
}

static void TriggerRandomEvent(RandomEventType type, NodeContainer ues, NodeContainer remoteHosts, Ptr<Node> gnb)
{
    double duration = 10.0; // Strictly 10 seconds

    if (type == EVENT_BLOCKAGE) {
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
                g_eventState.activeEvent = EVENT_BLOCKAGE;
                g_eventState.affectedPhy = phy;
                g_eventState.originalValue = phy->GetNoiseFigure();
                g_eventState.affectedIdx = ueIdx;
                
                double blockageNf = g_eventState.originalValue + 60.0; // +60dB noise figure
                phy->SetNoiseFigure(blockageNf);
                
                uint64_t imsi = ueDev->GetImsi();
                g_activeBlockage = true;
                g_blockageImsi = imsi;
                
                // --- NEW: Clean stdout one-liner ---
                std::cout << Simulator::Now().GetSeconds() << "s: [STDOUT] Blockage degradation occurred on UE " << imsi << std::endl;
                
                std::cerr << "\n"
                          << "╔════════════════════════════════════════════════════════════╗\n"
                          << "║  [EVENT] RANDOM BLOCKAGE TRIGGERED                       ║\n"
                          << "╠════════════════════════════════════════════════════════════╣\n"
                          << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                          << "║  Affected UE: " << std::setw(42) << imsi << " ║\n"
                          << "║  Original Noise Figure: " << std::setprecision(1) << std::setw(33) << g_eventState.originalValue << " dB ║\n"
                          << "║  Blockage Noise Figure: " << std::setw(33) << blockageNf << " dB ║\n"
                          << "║  Duration: " << std::setprecision(0) << std::setw(45) << duration << "s ║\n"
                          << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] Blockage on UE " << imsi << " (NF " << blockageNf << "dB)");
            }
        }
    } 
    else if (type == EVENT_TRAFFIC_SPIKE) {
        uint32_t rhIdx = rand() % remoteHosts.GetN();
        Ptr<Node> rh = remoteHosts.Get(rhIdx);
        
        std::vector<Ptr<OnOffApplication>> onOffApps;
        for (uint32_t i = 0; i < rh->GetNApplications(); ++i) {
            auto app = DynamicCast<OnOffApplication>(rh->GetApplication(i));
            if (app) onOffApps.push_back(app);
        }
        
        if (!onOffApps.empty()) {
            Ptr<OnOffApplication> onOffApp = onOffApps[rand() % onOffApps.size()];
            g_eventState.activeEvent = EVENT_TRAFFIC_SPIKE;
            g_eventState.affectedApp = onOffApp;
            g_eventState.affectedIdx = rhIdx;
            g_eventState.originalRate = DataRate("50Mbps");
            
            DataRate spikeRate("500Mbps");
            onOffApp->SetAttribute("DataRate", DataRateValue(spikeRate));
            
            g_activeTrafficSpike = true;
            g_trafficSpikeRhIdx = rhIdx;
            
            // --- NEW: Clean stdout one-liner ---
            std::cout << Simulator::Now().GetSeconds() << "s: [STDOUT] Traffic spike degradation occurred on RH " << rhIdx << std::endl;
            
            std::cerr << "\n"
                      << "╔════════════════════════════════════════════════════════════╗\n"
                      << "║  [EVENT] TRAFFIC SPIKE TRIGGERED                          ║\n"
                      << "╠════════════════════════════════════════════════════════════╣\n"
                      << "║  Time: " << std::fixed << std::setprecision(2) << std::setw(48) << Simulator::Now().GetSeconds() << "s ║\n"
                      << "║  Remote Host Index: " << std::setw(38) << rhIdx << " ║\n"
                      << "║  Original Data Rate: " << std::setw(38) << "50 Mbps ║\n"
                      << "║  Spike Data Rate: " << std::setw(40) << "500 Mbps ║\n"
                      << "║  Duration: " << std::setprecision(0) << std::setw(45) << duration << "s ║\n"
                      << "╚════════════════════════════════════════════════════════════╝\n" << std::endl;
            NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s: [EVENT] Traffic Spike on RH " << rhIdx << " (500Mbps)");
        }
    }

    // Schedule the restoration exactly 10 seconds later
    Simulator::Schedule(Seconds(duration), &RestoreRandomEvent, ues, remoteHosts, gnb);
}

static void ScheduleNextRandomEvent(NodeContainer ues, NodeContainer remoteHosts, Ptr<Node> gnb)
{
    // Stop scheduling if we are near the end of the simulation
    if (Simulator::Now().GetSeconds() >= (sim_duration - 15.0)) return;

    // 50/50 chance for Blockage vs Traffic Spike
    RandomEventType nextType = (rand() % 2 == 0) ? EVENT_BLOCKAGE : EVENT_TRAFFIC_SPIKE;
    
    // Trigger immediately (the cooldown was already handled by RestoreRandomEvent)
    TriggerRandomEvent(nextType, ues, remoteHosts, gnb);
}


// ---------------- main ----------------
int main (int argc, char** argv)
{
  CommandLine cmd;
  int rngSeed = 0;
  cmd.AddValue("rngSeed", "Seed for random number generator (default 0 = random)", rngSeed);
  cmd.Parse(argc, argv);

  if (rngSeed == 0) {
    srand(time(NULL));
  } else {
    srand(rngSeed);
  }

  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simTime = simV.Get();
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();

  // Read E2/Control GlobalValues
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

  NS_LOG_UNCOND ("e2lteEnabled " << e2lteEnabled << " e2nrEnabled " << e2nrEnabled << " e2du "
                                 << e2du << " e2cuCp " << e2cuCp << " e2cuUp " << e2cuUp
                                 << " controlFilename " << controlFilename
                                 << " useSemaphores " << useSemaphores
                                 << " indicationPeriodicity " << indicationPeriodicity
                                 << " reducedPmValues " << reducedPmValues
                                 << " e2TermIp " << e2TermIp);

  //-----------------------E2 CONFIGURATION----------------------------
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
  //-------------------------------------------------------------------

  // RF/system defaults
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
  mmw->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<Node> pgw = epc->GetPgwNode();

  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(2);
  NodeContainer rh;  rh.Create(1);

  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  // Mobility
  const Vector gnbPos = Vector(25,25,10);
  {
    MobilityHelper m;
    auto enbPos = CreateObject<ListPositionAllocator>();
    enbPos->Add(gnbPos);
    m.SetPositionAllocator(enbPos);
    m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    m.Install(gnb);
  }

  // ----------------------------------------------------------------
  //  Wall: X=[50,55], Y=[0,50], Z=[0,10]
  // ----------------------------------------------------------------
  Ptr<Building> wall = CreateObject<Building>();
  wall->SetBoundaries(Box(50.0, 55.0, 0.0, 50.0, 0.0, 10.0));
  wall->SetBuildingType(Building::Commercial);
  wall->SetExtWallsType(Building::ConcreteWithWindows);
  wall->SetNRoomsX(1);
  wall->SetNRoomsY(1);
  wall->SetNFloors(1);

  // UE Mobility 
  MobilityHelper uem;
  uem.SetMobilityModel("ns3::WaypointMobilityModel");
  uem.Install(ue);

  // -- UE0: Cyclic around wall, 55s cycle --
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(0)->GetObject<WaypointMobilityModel>();
    std::vector<Vector> pts = {
      Vector(30, 25, 1.5),
      Vector(45, 55, 1.5),
      Vector(65, 55, 1.5),
      Vector(70, 25, 1.5),
      Vector(95, 90, 1.5),
      Vector(48, 55, 1.5),
      Vector(30, 25, 1.5),
    };
    std::vector<double> offsets = {0, 8, 14, 24, 34, 44, 55};
    AddCyclicWaypoints(mob, pts, offsets, 55.0, simTime);
  }

  // -- UE1: Fast linear shuttle at Y=55, X=20..140, 30s round trip --
  {
    Ptr<WaypointMobilityModel> mob = ue.Get(1)->GetObject<WaypointMobilityModel>();
    double t = 0.0;
    bool outward = false;
    while (t < simTime) {
      double x = outward ? 140.0 : 20.0;
      mob->AddWaypoint(Waypoint(Seconds(t), Vector(x, 55.0, 1.5)));
      t += 15.0;
      outward = !outward;
    }
  }

  // Core + RH
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
  }

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

  const uint16_t cbrPort = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), cbrPort));
  ApplicationContainer sinkApps = sink.Install(ue); // Install on all UEs
  sinkApps.Start(Seconds(0.2));
  
  OnOffHelper cbr("ns3::UdpSocketFactory", InetSocketAddress(ueIf.GetAddress(0), cbrPort));
  cbr.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  cbr.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  cbr.SetAttribute("DataRate", StringValue("50Mbps"));
  cbr.SetAttribute("PacketSize", UintegerValue(1200));

  // Install traffic for all UEs
  for (uint32_t i = 0; i < ue.GetN(); ++i) {
      cbr.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIf.GetAddress(i), cbrPort)));
      cbr.Install(rh.Get(0)).Start(Seconds(0.35));
  }

  // Install Ping Application
  V4PingHelper ping(ueIf.GetAddress(0));
  ping.SetAttribute("Interval", TimeValue(Seconds(0.1)));
  ping.SetAttribute("Size", UintegerValue(56));
  ping.SetAttribute("Verbose", BooleanValue(false));
  ApplicationContainer pingApps = ping.Install(rh.Get(0));
  pingApps.Start(Seconds(0.5));
  
  // Connect Ping RTT trace
  Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt", MakeCallback(&PingRttCallback));

  mmw->EnableTraces();

  const double covRadius = 100.0;
  Simulator::Schedule(Seconds(0.1), &SampleAll,
                      std::ref(ue), std::ref(ueDevs), gnb.Get(0),
                      covRadius, sinkApps, 0.1);

  // Start progress printer
  Simulator::Schedule(Seconds(0.0), &PrintProgress);

  // NetAnim
  AnimationInterface anim("NetAnimFile_v3.xml");
  g_anim = &anim;
  anim.SetMobilityPollInterval(Seconds(1));
  anim.SkipPacketTracing();

  // Schedule MCS Events (Existing)
  //Simulator::Schedule(Seconds(5.0), &ScheduleNextMcsEvent, gnb.Get(0), true);

  // start the random event scheduler
  Simulator::Schedule(Seconds(12.0), &ScheduleNextRandomEvent, ue, rh, gnb.Get(0));

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
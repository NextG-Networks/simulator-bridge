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
#include "ns3/constant-velocity-mobility-model.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <ctime>

using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_1gNB_3UE_uniform"); // Simple hybrid scenario test

// --- Runtime flags (simple) ---
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(30.0), MakeDoubleChecker<double>(1.0, 3600.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

// ---- Shared state for uniform sampler ----
struct SamplerState {
  // Throughput (UE0 sink)
  uint64_t lastBytes = 0; // last total bytes seen
  double   lastT = 0.0;     // last time sampled (s)
  double   ewma = 0.0;   // EWMA smoothed throughput (Mbps)
  // Ping
  double   lastPingMs = 0.0;   // “latest known” RTT; stays constant until next reply
  bool     seenPing = false;
};
static SamplerState gS;


// --- Ping RTT callback: update shared state, do NOT write the file here ---
static void PingRttCallback(Time rtt) {
  gS.lastPingMs = rtt.GetMilliSeconds();
  gS.seenPing   = true;
}

// --- One uniform sampler writing a single wide CSV row every tick ---
static void SampleAll(const NodeContainer &ueNodes,
                      const NetDeviceContainer &ueDevs,
                      Ptr<Node> gnbNode,
                      double covRadius,
                      Ptr<PacketSink> sink0,
                      double periodSec)
{
  static std::ofstream f;
  static bool headerDone = false;

  // open + header once 
  if (!headerDone) {// Information that will be logged in the CSV file
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
      << "\n";
    headerDone = true;
  }

  // Get current time and gNB position
  const double now = Simulator::Now().GetSeconds();
  Vector gp = gnbNode->GetObject<MobilityModel>()->GetPosition();

  // start row
  f << now;

  // per-UE geometry
  // For each UE, get position, distance to gNB
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

  // UE0 throughput (bytes delta since last sample)
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

  // EWMA smoother with time-constant tau (seconds)
  const double tau = 1.0;                      // tweak if you want smoother/faster
  const double alpha = 1.0 - std::exp(-periodSec / tau);
  gS.ewma = alpha * mbps + (1.0 - alpha) * gS.ewma;

  // Ping: use last seen RTT (constant until next reply; 0.0 before first)
  double pingMs = gS.seenPing ? gS.lastPingMs : 0.0;

  // finish row
  f << "," << mbps
    << "," << gS.ewma
    << "," << pingMs
    << "\n";
  f.flush();

  // reschedule
  Simulator::Schedule(Seconds(periodSec), &SampleAll,
                      ueNodes, ueDevs, gnbNode, covRadius, sink0, periodSec);
}

int main (int argc, char** argv)
{
  // Parse command line
  CommandLine cmd; cmd.Parse(argc, argv);
  // Setup so we get different runs each time with seed based on time
  SeedManager::SetSeed(std::time(nullptr));

  // Read flags
  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV); 
  double simTime = simV.Get();
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();

  // Output dir
  fs::create_directories(outDir);
  fs::current_path(outDir);

  // RF & system defaults (inside main)
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth",  DoubleValue(100e6));
  Config::SetDefault("ns3::MmWaveEnbPhy::TxPower",          DoubleValue(23.0)); // dBm

  // Coverage disc + world bounds
  const Vector gnbPos = Vector(0,0,10);
  const double covRadius = 100.0;
  const double worldHalf = 450.0;

  // Helpers

  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>(); 
  mmw->SetPathlossModelType ("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmw->SetChannelConditionModelType ("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  Ptr<Node> pgw = epc->GetPgwNode();

  // Creates the different nodes: gNB, UEs, RemoteHost
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(1);
  NodeContainer rh;  rh.Create(1);

  // Internet stacks
  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

  // gNB fixed
  MobilityHelper gMob;
  auto enbPos = CreateObject<ListPositionAllocator>();
  enbPos->Add(gnbPos);
  gMob.SetPositionAllocator(enbPos);
  gMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  gMob.Install(gnb);


  // For having random walk in the disc
  // // UEs: seed inside coverage disc then RandomWalk
  // MobilityHelper uMob;
  // Ptr<UniformDiscPositionAllocator> disc = CreateObject<UniformDiscPositionAllocator>();
  // disc->SetX(gnbPos.x);
  // disc->SetY(gnbPos.y);
  // disc->SetRho(covRadius * 0.9);
  // uMob.SetPositionAllocator(disc);
  // uMob.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
  //                       "Mode",   StringValue("Time"),
  //                       "Time",   TimeValue(Seconds(1.0)),
  //                       "Speed",  StringValue("ns3::UniformRandomVariable[Min=0.8|Max=2.0]"),
  //                       "Bounds", RectangleValue(Rectangle(-worldHalf, worldHalf,
  //                                                          -worldHalf, worldHalf)));
  // uMob.Install(ue);
  
  // UEs: Start near gNB and move directly away
  MobilityHelper uMob;
uMob.SetMobilityModel("ns3::ConstantVelocityMobilityModel");

// Choose start positions. Here, put every UE 1 m from the gNB, spaced by angle.
// If you only have 1 UE, it will start at (1,0,0).
Ptr<ListPositionAllocator> startPos = CreateObject<ListPositionAllocator>();
for (uint32_t i = 0; i < ue.GetN(); ++i)
{
  // double ang = 0.0 + i * (M_PI / 6.0); // 30° spacing; change as you like
  // startPos->Add(Vector(50.0 * std::cos(ang), 50.0 * std::sin(ang), 0.0)); // z=0
  startPos->Add(Vector(50.0, 1.0, 0.0)); // z=0

}
uMob.SetPositionAllocator(startPos);
uMob.Install(ue);
// After install, set velocities. Make each UE move away from the gNB (0,0,10) in XY.
// Pick a speed, e.g., 10 m/s.
const double speed = 10.0;
for (uint32_t i = 0; i < ue.GetN(); ++i)
{
  Ptr<ConstantVelocityMobilityModel> m = ue.Get(i)->GetObject<ConstantVelocityMobilityModel>();
  NS_ASSERT_MSG(m, "ConstantVelocityMobilityModel not found on UE");

  Vector p = m->GetPosition();
  // Direction in XY away from gNB; ignore Z (keep z constant).
  double dx = p.x - gnbPos.x;
  double dy = p.y - gnbPos.y;
  double norm = std::sqrt(dx*dx + dy*dy);
  // If a UE happens to start *exactly* above the gNB (norm=0), push it along +X.
  double vx = (norm > 0.0 ? speed * dx / norm : speed);
  double vy = (norm > 0.0 ? speed * dy / norm : 0.0);
  m->SetVelocity(Vector(vx, vy, 0.0));
}


  // Devices
  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);

  // EPC addressing for UEs
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ue.GetN(); ++u) {
    Ptr<Ipv4StaticRouting> r = srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>());
    r->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  }

  // Attach UEs
  mmw->AttachToClosestEnb(ueDevs, gnbDevs);

  // Backhaul: PGW <-> RemoteHost
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gb/s")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer d = p2p.Install(pgw, rh.Get(0));

  Ipv4AddressHelper a; a.SetBase("10.0.0.0","255.0.0.0");
  Ipv4InterfaceContainer ifs = a.Assign(d);
  Ipv4StaticRoutingHelper srh;
  srh.GetStaticRouting(rh.Get(0)->GetObject<Ipv4>())
     ->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // --- Traffic: DL UDP CBR (RH -> UE0) ---
  const uint16_t cbrPort = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), cbrPort));
  ApplicationContainer sinkApps = sink.Install(ue.Get(0));
  sinkApps.Start(Seconds(0.2));
  Ptr<PacketSink> sinkApp = DynamicCast<PacketSink>(sinkApps.Get(0));

  OnOffHelper cbr("ns3::UdpSocketFactory",
                  InetSocketAddress(ueIf.GetAddress(0), cbrPort));
  cbr.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  cbr.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  cbr.SetAttribute("DataRate", StringValue("50Mbps"));
  cbr.SetAttribute("PacketSize", UintegerValue(1200));
  cbr.Install(rh.Get(0)).Start(Seconds(0.35));  // start a bit earlier to reduce initial zeros

  // --- Ping: RH -> UE0 (fixed 1s interval), latch RTT via callback ---
  V4PingHelper ping(ueIf.GetAddress(0));
  ping.SetAttribute("Verbose", BooleanValue(false));
  ping.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  ApplicationContainer p = ping.Install(rh.Get(0));
  p.Start(Seconds(0.6));
  Ptr<V4Ping> pingApp = DynamicCast<V4Ping>(p.Get(0));
  pingApp->TraceConnectWithoutContext("Rtt", MakeCallback(&PingRttCallback));

  // Enable mmWave traces if you still want the module KPIs
  mmw->EnableTraces();

  // Start the single uniform sampler (0.1 s)
  Simulator::Schedule(Seconds(0.1), &SampleAll,
                      std::ref(ue), std::ref(ueDevs), gnb.Get(0),
                      covRadius, sinkApp, 0.1);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

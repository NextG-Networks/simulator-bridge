/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/mmwave-helper.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include <filesystem>
#include <fstream>
#include <cmath>


using namespace ns3;
using namespace mmwave;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("MVS_Mmwave_1gNB_1UE");

// Simple runtime flags
static GlobalValue g_simTime("simTime", "Simulation time (s)",
  DoubleValue(10.0), MakeDoubleChecker<double>(1.0, 3600.0));
static GlobalValue g_outDir("outDir", "Output directory",
  StringValue("out/logs"), MakeStringChecker());

// Periodically dump UE positions (and distance to the gNB) to ue_positions.csv
static void SamplePositions(ns3::NodeContainer ueNodes,
                            ns3::NetDeviceContainer ueDevs,
                            ns3::Ptr<ns3::Node> gnbNode,
                            double periodSec)
{
  static std::ofstream f("ue_positions.csv", std::ios::out | std::ios::trunc);
  static bool header = false;
  if (!header) {
    f << "time_s,ue_index,imsi,x,y,z,dist_to_gnb_m\n";
    header = true;
  }

  double t = ns3::Simulator::Now().GetSeconds();
  auto gmm = gnbNode->GetObject<ns3::MobilityModel>();
  ns3::Vector gp = gmm->GetPosition();

  for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
    auto mm = ueNodes.Get(i)->GetObject<ns3::MobilityModel>();
    ns3::Vector p = mm->GetPosition();
    double dx = p.x - gp.x, dy = p.y - gp.y, dz = p.z - gp.z;
    double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    //auto imsi = ueDevs.Get(i)->GetObject<ns3::MmWaveUeNetDevice>()->GetImsi();
    f << t << "," << i << "," << "," << p.x << "," << p.y << "," << p.z << "," << dist << "\n";
  }
  f.flush();
  ns3::Simulator::Schedule(ns3::Seconds(periodSec), &SamplePositions, ueNodes, ueDevs, gnbNode, periodSec);
}

  
int main (int argc, char** argv)
{
  CommandLine cmd; cmd.Parse(argc, argv);

  // read flags
  DoubleValue simV; GlobalValue::GetValueByName("simTime", simV);
  double simTime = simV.Get();
  StringValue outV; GlobalValue::GetValueByName("outDir", outV);
  fs::path outDir = outV.Get();

  // ensure output folder and route ALL default-named KPIs there (no attr name fights)
  fs::create_directories(outDir);
  fs::current_path(outDir);

  // (optional) capture console logs here too
  // std::freopen("stdout.txt","w",stdout);
  // std::freopen("stderr.txt","w",stderr);

  // Helpers
  Ptr<MmWaveHelper> mmw = CreateObject<MmWaveHelper>();
  Ptr<MmWavePointToPointEpcHelper> epc = CreateObject<MmWavePointToPointEpcHelper>();
  mmw->SetEpcHelper(epc);
  Ptr<Node> pgw = epc->GetPgwNode();

  // Nodes
  NodeContainer gnb; gnb.Create(1);
  NodeContainer ue;  ue.Create(3);
  NodeContainer rh;  rh.Create(1); // remote host

  // Internet stacks
  InternetStackHelper ip; ip.Install(ue); ip.Install(rh);

 // Mobility: fixed gNB; UE does RandomWalk2d in a box
{
  // gNB fixed at (0,0,10)
  MobilityHelper m;
  auto enbPos = CreateObject<ListPositionAllocator>();
  enbPos->Add(Vector(0,0,10));
  m.SetPositionAllocator(enbPos);
  m.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  m.Install(gnb);

  // UE: startpos + RandomWalk2d
  MobilityHelper uem;
  auto uePos = CreateObject<ListPositionAllocator>();
  uePos->Add(Vector(50,0,1.5));                // start position
  uem.SetPositionAllocator(uePos);

  Ptr<UniformRandomVariable> speed = CreateObject<UniformRandomVariable>();
  speed->SetAttribute("Min", DoubleValue(0.5)); // m/s
  speed->SetAttribute("Max", DoubleValue(2.0)); // m/s

  uem.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                       "Mode",   StringValue("Time"),
                       "Time",   TimeValue(Seconds(1.0)),            // ny riktning varje sekund
                       "Speed",  PointerValue(speed),
                       "Bounds", RectangleValue(Rectangle(-120,120,-120,120)));
  uem.Install(ue);
}


  // Devices (PURE mmWave: no McUe, no LTE anchor)
  NetDeviceContainer gnbDevs = mmw->InstallEnbDevice(gnb);
  NetDeviceContainer ueDevs  = mmw->InstallUeDevice(ue);

  SamplePositions(ue, ueDevs, gnb.Get(0), 0.1);

  // IP addressing for UEs via EPC
  Ipv4InterfaceContainer ueIf = epc->AssignUeIpv4Address(ueDevs);
  Ipv4StaticRoutingHelper srt;
  for (uint32_t u=0; u<ue.GetN(); ++u) {
    Ptr<Ipv4StaticRouting> r = srt.GetStaticRouting(ue.Get(u)->GetObject<Ipv4>());
    r->SetDefaultRoute(epc->GetUeDefaultGatewayAddress(), 1);
  }

  // Attach UE to gNB
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

  // Traffic: UDP Echo (RH -> UE)
  uint16_t port = 9;
  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(ue.Get(0));
  serverApps.Start(Seconds(0.2));

  UdpEchoClientHelper echoClient(ueIf.GetAddress(0), port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(50));
  echoClient.SetAttribute("Interval", TimeValue(MilliSeconds(100))); // 10 pps
  echoClient.SetAttribute("PacketSize", UintegerValue(200));
  ApplicationContainer clientApps = echoClient.Install(rh.Get(0));
  clientApps.Start(Seconds(0.5));

  // Basic stats from mmwave side (creates calculators with default filenames)
  mmw->EnableTraces();

  // (Optional) quick labels to verify positions
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

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * GPLv2
 * Authors: Andrea Lacava, Michele Polese
 * (LTE-only variant compatible with EPC; IdealRrc/trace helpers removed)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"

#include "ns3/lte-helper.h"
#include "ns3/lte-ue-net-device.h"
#include "ns3/lte-enb-net-device.h"     // needed for GetCellId()
#include "ns3/epc-helper.h"
#include "ns3/point-to-point-epc-helper.h"
#include "ns3/eps-bearer.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ScenarioOne");

/*** Small helpers to dump node labels for gnuplot ***/
static void
PrintGnuplottableUeListToFile (const std::string &filename)
{
  std::ofstream out (filename.c_str (), std::ios_base::trunc);
  if (!out.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> n = *it;
      for (uint32_t j = 0; j < n->GetNDevices (); ++j)
        {
          Ptr<LteUeNetDevice> ue = n->GetDevice (j)->GetObject<LteUeNetDevice> ();
          if (!ue) continue;
          Ptr<MobilityModel> mob = n->GetObject<MobilityModel> ();
          if (!mob) continue;
          Vector p = mob->GetPosition ();
          out << "set label \"" << ue->GetImsi () << "\" at "
              << p.x << "," << p.y
              << " left font \"Helvetica,8\" textcolor rgb \"black\" front "
                 "point pt 1 ps 0.3 lc rgb \"black\" offset 0,0\n";
        }
    }
}

static void
PrintGnuplottableEnbListToFile (const std::string &filename)
{
  std::ofstream out (filename.c_str (), std::ios_base::trunc);
  if (!out.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> n = *it;
      for (uint32_t j = 0; j < n->GetNDevices (); ++j)
        {
          Ptr<LteEnbNetDevice> enb = n->GetDevice (j)->GetObject<LteEnbNetDevice> ();
          if (!enb) continue;
          Ptr<MobilityModel> mob = n->GetObject<MobilityModel> ();
          if (!mob) continue;
          Vector p = mob->GetPosition ();
          out << "set label \"" << enb->GetCellId () << "\" at "
              << p.x << "," << p.y
              << " left font \"Helvetica,8\" textcolor rgb \"blue\" front "
                 "point pt 4 ps 0.3 lc rgb \"blue\" offset 0,0\n";
        }
    }
}

/*** Global values to keep CLI compatibility with your environment ***/
static GlobalValue g_bufferSize ("bufferSize", "RLC tx buffer size (MB)",
                                 UintegerValue (10), MakeUintegerChecker<uint32_t> ());
static GlobalValue g_rlcAmEnabled ("rlcAmEnabled", "If true, use RLC AM, else UM",
                                   BooleanValue (true), MakeBooleanChecker ());
static GlobalValue g_enableTraces ("enableTraces", "If true, generate ns-3 traces (limited under EPC)",
                                   BooleanValue (true), MakeBooleanChecker ());
static GlobalValue g_e2lteEnabled ("e2lteEnabled", "If true, send LTE E2 reports",
                                   BooleanValue (true), MakeBooleanChecker ());
static GlobalValue g_e2nrEnabled  ("e2nrEnabled",  "NR E2 (unused here)",
                                   BooleanValue (false), MakeBooleanChecker ());
static GlobalValue g_e2du   ("e2du",   "DU reports (LTE only here)", BooleanValue (true),  MakeBooleanChecker ());
static GlobalValue g_e2cuUp ("e2cuUp", "CU-UP reports",              BooleanValue (true),  MakeBooleanChecker ());
static GlobalValue g_e2cuCp ("e2cuCp", "CU-CP reports",              BooleanValue (true),  MakeBooleanChecker ());

static GlobalValue g_trafficModel ("trafficModel",
  "0 full-buffer DL; 1 mixed; 2 bursty UL; 3 mixed tiers",
  UintegerValue (0), MakeUintegerChecker<uint8_t> ());
static GlobalValue g_configuration ("configuration", "0..2",
  UintegerValue (0), MakeUintegerChecker<uint8_t> ());
static GlobalValue g_hoSinrDifference ("hoSinrDifference", "unused in single-eNB",
  DoubleValue (3.0), MakeDoubleChecker<double> ());
static GlobalValue g_dataRate ("dataRate", "0 low, 1 high",
  DoubleValue (0.0), MakeDoubleChecker<double> (0.0, 1.0));
static GlobalValue g_ues ("ues", "Total number of UEs",
  UintegerValue (1), MakeUintegerChecker<uint32_t> ());
static GlobalValue g_indicationPeriodicity ("indicationPeriodicity", "E2 period [s]",
  DoubleValue (0.1), MakeDoubleChecker<double> (0.01, 2.0));
static GlobalValue g_simTime ("simTime", "Simulation time [s]",
  DoubleValue (1.9), MakeDoubleChecker<double> (0.1, 1000.0));
static GlobalValue g_reducedPmValues ("reducedPmValues", "Reduced PM set",
  BooleanValue (true), MakeBooleanChecker ());
static GlobalValue g_outageThreshold ("outageThreshold", "SNR threshold [dB]",
  DoubleValue (-1000.0), MakeDoubleChecker<double> ());
static GlobalValue g_basicCellId ("basicCellId", "First cellId",
  UintegerValue (1), MakeUintegerChecker<uint16_t> ());
static GlobalValue g_handoverMode ("handoverMode", "unused in single-eNB",
  StringValue ("NoAuto"), MakeStringChecker ());
static GlobalValue g_e2TermIp ("e2TermIp", "RIC E2 termination IP",
  StringValue ("10.244.0.240"), MakeStringChecker ());
static GlobalValue g_enableE2FileLogging ("enableE2FileLogging", "Offline E2 logs",
  BooleanValue (true), MakeBooleanChecker ());
static GlobalValue g_useSemaphores ("useSemaphores", "External control",
  BooleanValue (false), MakeBooleanChecker ());
static GlobalValue g_controlFileName ("controlFileName", "Control file path",
  StringValue ("ts_actions_for_ns3.csv"), MakeStringChecker ());
static GlobalValue g_minSpeed ("minSpeed", "UE min speed [m/s]",
  DoubleValue (2.0), MakeDoubleChecker<double> ());
static GlobalValue g_maxSpeed ("maxSpeed", "UE max speed [m/s]",
  DoubleValue (4.0), MakeDoubleChecker<double> ());

int
main (int argc, char *argv[])
{
  LogComponentEnableAll (LOG_PREFIX_ALL);
  CommandLine cmd; cmd.Parse (argc, argv);

  /* Read CLI globals */
  BooleanValue b; UintegerValue u; DoubleValue d; StringValue s;

  GlobalValue::GetValueByName ("rlcAmEnabled", b); bool rlcAm = b.Get ();
  GlobalValue::GetValueByName ("bufferSize", u); uint32_t bufMb = u.Get ();
  GlobalValue::GetValueByName ("trafficModel", u); uint8_t trafficModel = u.Get ();
  GlobalValue::GetValueByName ("outageThreshold", d); double outageThr = d.Get ();
  GlobalValue::GetValueByName ("handoverMode", s); std::string hoMode = s.Get ();
  GlobalValue::GetValueByName ("basicCellId", u); uint16_t basicCellId = u.Get ();
  GlobalValue::GetValueByName ("e2TermIp", s); std::string e2TermIp = s.Get ();
  GlobalValue::GetValueByName ("enableE2FileLogging", b); bool e2File = b.Get ();
  GlobalValue::GetValueByName ("minSpeed", d); double minSpeed = d.Get ();
  GlobalValue::GetValueByName ("maxSpeed", d); double maxSpeed = d.Get ();
  GlobalValue::GetValueByName ("indicationPeriodicity", d); double e2Per = d.Get ();
  GlobalValue::GetValueByName ("useSemaphores", b); bool useSem = b.Get ();
  GlobalValue::GetValueByName ("controlFileName", s); std::string ctrlFile = s.Get ();
  GlobalValue::GetValueByName ("ues", u); uint32_t nUe = u.Get ();
  GlobalValue::GetValueByName ("configuration", u); uint8_t cfg = u.Get ();
  GlobalValue::GetValueByName ("dataRate", d); double rateSel = d.Get ();
  GlobalValue::GetValueByName ("simTime", d); double simTime = d.Get ();

  GlobalValue::GetValueByName ("e2lteEnabled", b); bool e2lte = b.Get ();
  GlobalValue::GetValueByName ("e2nrEnabled", b);  bool e2nr  = b.Get ();
  GlobalValue::GetValueByName ("e2du", b);         bool e2du  = b.Get ();
  GlobalValue::GetValueByName ("e2cuUp", b);       bool e2cuUp= b.Get ();
  GlobalValue::GetValueByName ("e2cuCp", b);       bool e2cuCp= b.Get ();
  GlobalValue::GetValueByName ("reducedPmValues", b); bool redPm = b.Get ();

  NS_LOG_UNCOND ("rlcAm " << rlcAm << " bufMB " << bufMb
                 << " traffic " << unsigned(trafficModel)
                 << " outage " << outageThr << " hoMode " << hoMode
                 << " basicCellId " << basicCellId
                 << " e2TermIp " << e2TermIp << " e2File " << e2File
                 << " minSpeed " << minSpeed << " maxSpeed " << maxSpeed
                 << " nUe " << nUe);
  NS_LOG_UNCOND ("e2lte " << e2lte << " e2nr " << e2nr
                 << " e2du " << e2du << " e2cuCp " << e2cuCp
                 << " e2cuUp " << e2cuUp << " redPm " << redPm
                 << " ctrlFile " << ctrlFile << " e2Per " << e2Per
                 << " useSem " << useSem);

  /* LTE + E2 defaults (LTE only, EPC in use → no IdealRrc here) */
  Config::SetDefault ("ns3::LteEnbNetDevice::ControlFileName", StringValue (ctrlFile));
  Config::SetDefault ("ns3::LteEnbNetDevice::UseSemaphores",   BooleanValue (useSem));
  Config::SetDefault ("ns3::LteEnbNetDevice::E2Periodicity",   DoubleValue (e2Per));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuUpReport",BooleanValue (e2cuUp));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuCpReport",BooleanValue (e2cuCp));
  Config::SetDefault ("ns3::LteEnbNetDevice::ReducedPmValues", BooleanValue (redPm));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableE2FileLogging", BooleanValue (e2File));

  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize",       UintegerValue (bufMb * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue (bufMb * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize",       UintegerValue (bufMb * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer",     TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::LteEnbRrc::OutageThreshold",      DoubleValue (outageThr));

  /* Simple presets from configuration (just affects UE spread + app rate) */
  double isd = 1000.0;
  std::string appRate;
  switch (cfg)
    {
    case 0: isd = 1000.0; appRate = (rateSel == 0.0 ? "1.5Mbps" : "4.5Mbps"); break;
    case 1: isd = 1000.0; appRate = (rateSel == 0.0 ? "1.5Mbps" : "4.5Mbps"); break;
    case 2: isd =  200.0; appRate = (rateSel == 0.0 ? "15Mbps" : "45Mbps");   break;
    default: NS_FATAL_ERROR ("Unknown configuration " << unsigned(cfg));
    }

  /* EPC + Internet */
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
  Ptr<Node> pgw = epcHelper->GetPgwNode ();

  InternetStackHelper internet;
  NodeContainer remoteHostContainer; remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  internet.Install (remoteHostContainer);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2p.SetDeviceAttribute ("Mtu",      UintegerValue (2500));
  p2p.SetChannelAttribute ("Delay",   TimeValue (Seconds (0.010)));
  NetDeviceContainer internetDevices = p2p.Install (pgw, remoteHost);

  Ipv4AddressHelper ipv4; ipv4.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer ifaces = ipv4.Assign (internetDevices);
  Ipv4Address remoteHostAddr = ifaces.GetAddress (1);

  Ipv4StaticRoutingHelper ipv4Rh;
  Ptr<Ipv4StaticRouting> rhRouting =
      ipv4Rh.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  rhRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  /* Nodes */
  NodeContainer enbNodes; enbNodes.Create (1);
  NodeContainer ueNodes;  ueNodes.Create  (nUe);

  /* Mobility */
  Vector center (2000.0, 2000.0, 3.0);
  Ptr<ListPositionAllocator> enbPos = CreateObject<ListPositionAllocator> ();
  enbPos->Add (center);
  MobilityHelper enbMob;
  enbMob.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  enbMob.SetPositionAllocator (enbPos);
  enbMob.Install (enbNodes);

  Ptr<UniformDiscPositionAllocator> uePos = CreateObject<UniformDiscPositionAllocator> ();
  uePos->SetX (center.x); uePos->SetY (center.y); uePos->SetRho (isd);
  Ptr<UniformRandomVariable> speed = CreateObject<UniformRandomVariable> ();
  speed->SetAttribute ("Min", DoubleValue (minSpeed));
  speed->SetAttribute ("Max", DoubleValue (maxSpeed));
  MobilityHelper ueMob;
  ueMob.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                          "Speed", PointerValue (speed),
                          "Bounds", RectangleValue (Rectangle (0, 4000, 0, 4000)));
  ueMob.SetPositionAllocator (uePos);
  ueMob.Install (ueNodes);

  /* LTE devices (no IdealRrc when EPC is used) */
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  lteHelper->SetEpcHelper (epcHelper);

  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice  (ueNodes);

  /* IP on UEs */
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIfaces = epcHelper->AssignUeIpv4Address (ueDevs);

  /* Attach and activate default bearer (2-arg API in this tree) */
  lteHelper->Attach (ueDevs, enbDevs.Get (0));
  EpsBearer bearer (EpsBearer::NGBR_VIDEO_TCP_DEFAULT);
  //lteHelper->ActivateDataRadioBearer (ueDevs, bearer);

  /* Default route on each UE */
  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      Ptr<Ipv4StaticRouting> ueRt =
          ipv4Rh.GetStaticRouting (ueNodes.Get (i)->GetObject<Ipv4> ());
      ueRt->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  /* Applications */
  uint16_t portTcp = 50000, portUdp = 60000;
  ApplicationContainer sinkApps;

  // uplink sinks on remote host
  PacketSinkHelper sinkTcp ("ns3::TcpSocketFactory",
                            InetSocketAddress (Ipv4Address::GetAny (), portTcp));
  sinkApps.Add (sinkTcp.Install (remoteHost));
  PacketSinkHelper sinkUdp ("ns3::UdpSocketFactory",
                            InetSocketAddress (Ipv4Address::GetAny (), portUdp));
  sinkApps.Add (sinkUdp.Install (remoteHost));

  // UL clients on UEs
  OnOffHelper onoffTcp ("ns3::TcpSocketFactory",
                        Address (InetSocketAddress (remoteHostAddr, portTcp)));
  onoffTcp.SetAttribute ("OnTime",  StringValue ("ns3::ExponentialRandomVariable"));
  onoffTcp.SetAttribute ("OffTime", StringValue ("ns3::ExponentialRandomVariable"));
  onoffTcp.SetAttribute ("DataRate", StringValue ("1.5Mbps"));
  onoffTcp.SetAttribute ("PacketSize", UintegerValue (1280));
  if (rateSel > 0.0) onoffTcp.SetAttribute ("DataRate", StringValue ("4.5Mbps"));

  OnOffHelper onoffTcp150 = onoffTcp; onoffTcp150.SetAttribute ("DataRate", StringValue ("150kbps"));
  OnOffHelper onoffTcp750 = onoffTcp; onoffTcp750.SetAttribute ("DataRate", StringValue ("750kbps"));

  OnOffHelper onoffUdp ("ns3::UdpSocketFactory",
                        Address (InetSocketAddress (remoteHostAddr, portUdp)));
  onoffUdp.SetAttribute ("OnTime",  StringValue ("ns3::ExponentialRandomVariable"));
  onoffUdp.SetAttribute ("OffTime", StringValue ("ns3::ExponentialRandomVariable"));
  onoffUdp.SetAttribute ("DataRate", StringValue (rateSel == 0.0 ? "1.5Mbps" : "4.5Mbps"));
  onoffUdp.SetAttribute ("PacketSize", UintegerValue (1280));

  // DL full-buffer from remote host (port 1234)
  ApplicationContainer clientApps;
  switch (trafficModel)
    {
    case 0: // all DL full-buffer
      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          PacketSinkHelper dlSink ("ns3::UdpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), 1234));
          sinkApps.Add (dlSink.Install (ueNodes.Get (i)));

          UdpClientHelper dlClient (ueIfaces.GetAddress (i), 1234);
          dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds (500)));
          dlClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
          dlClient.SetAttribute ("PacketSize", UintegerValue (1280));
          clientApps.Add (dlClient.Install (remoteHost));
        }
      break;

    case 1: // half DL full-buffer, half bursty UL
      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          if (i % 2 == 0)
            {
              if (i % 4 == 0) clientApps.Add (onoffTcp.Install (ueNodes.Get (i)));
              else            clientApps.Add (onoffUdp.Install (ueNodes.Get (i)));
            }
          else
            {
              PacketSinkHelper dlSink ("ns3::UdpSocketFactory",
                                       InetSocketAddress (Ipv4Address::GetAny (), 1234));
              sinkApps.Add (dlSink.Install (ueNodes.Get (i)));
              UdpClientHelper dlClient (ueIfaces.GetAddress (i), 1234);
              dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds (500)));
              dlClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
              dlClient.SetAttribute ("PacketSize", UintegerValue (1280));
              clientApps.Add (dlClient.Install (remoteHost));
            }
        }
      break;

    case 2: // all bursty UL
      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          if (i % 2 == 0) clientApps.Add (onoffTcp.Install (ueNodes.Get (i)));
          else            clientApps.Add (onoffUdp.Install (ueNodes.Get (i)));
        }
      break;

    case 3: // 25% DL full-buffer + three UL tiers
      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          if (i % 4 == 0)
            {
              PacketSinkHelper dlSink ("ns3::UdpSocketFactory",
                                       InetSocketAddress (Ipv4Address::GetAny (), 1234));
              sinkApps.Add (dlSink.Install (ueNodes.Get (i)));

              UdpClientHelper dlClient (ueIfaces.GetAddress (i), 1234);
              dlClient.SetAttribute ("PacketSize", UintegerValue (1280));
              dlClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
              dlClient.SetAttribute ("Interval",
                TimeValue (MicroSeconds (cfg == 2 ? 250 : 500)));
              clientApps.Add (dlClient.Install (remoteHost));
            }
          else if (i % 4 == 1)
            {
              clientApps.Add (onoffTcp.Install (ueNodes.Get (i)));
            }
          else if (i % 4 == 2)
            {
              clientApps.Add (onoffTcp750.Install (ueNodes.Get (i)));
            }
          else
            {
              clientApps.Add (onoffTcp150.Install (ueNodes.Get (i)));
            }
        }
      break;

    default: NS_FATAL_ERROR ("Unknown trafficModel " << unsigned(trafficModel));
    }

  /* Start/stop */
  sinkApps.Start (Seconds (0.0));
  clientApps.Start (MilliSeconds (100));
  clientApps.Stop  (Seconds (simTime - 0.1));

  /* NOTE: we do NOT call lteHelper->Initialize()/Enable*Traces() here,
     because those assert when EPC is in use. If you need traces, connect
     to the specific trace sources via Config::Connect*. */

  PrintGnuplottableUeListToFile ("ues.txt");
  PrintGnuplottableEnbListToFile ("enbs.txt");

  NS_LOG_UNCOND ("Simulation time is " << simTime << " s");
  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}

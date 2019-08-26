/*
 * Copyright (c) 2015-2019 IMDEA Networks Institute
 * Author: Hany Assasa <hany.assasa@gmail.com>
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"
#include "common-functions.h"
#include <iomanip>
#include <sstream>

/**
 * Simulation Objective:
 * This script is used to evaluate the performance and behaviour of the IEEE 802.11ad standard over Q-D channel model.
 * Both DMG AP and DMG STAs use a parametric codebook generated by our IEEE 802.11ad Codebook Generator in MATLAB.
 * Each device uses an URA antenna array of 2x8 Elements. The channel model is generated by our Q-D Realization software.
 * We use this script to generate the dense Scenario results in our paper "High Fidelity Simulation of IEEE 802.11ad
 * in ns-3 Using a Quasi-deterministic Channel Model".
 *
 * Network Topology:
 * The network consists of a single access point in the cneter of a room surrounded by 10 DMG STAs.
 *
 *
 *                                 DMG STA (10)
 *
 *
 *
 *                  DMG STA (1)                     DMG STA (9)
 *

 *
 *          DMG STA (2)                                     DMG STA (8)
 *
 *                                    DMG AP
 *
 *          DMG STA (3)                                     DMG STA (7)
 *

 *
 *                  DMG STA (4)                     DMG STA (6)
 *

 *
 *                                  DMG STA (5)
 *
 *
 * Running the Simulation:
 * ./waf --run "evaluate_qd_dense_scenario_single_ap"
 *
 * Simulation Output:
 * The simulation generates the following traces:
 * 1. PCAP traces for each station.
 * 2. SLS results for visualization in Q-D Visualizer.
 * 3. SNR Information for TxSS phases.
 * 4. SNR Information for data packets if enabled.
 */


NS_LOG_COMPONENT_DEFINE ("EvaluateQdPropagationLossModel");

using namespace ns3;
using namespace std;

typedef std::map<Mac48Address, uint32_t> MAP_MAC2ID;
typedef MAP_MAC2ID::iterator MAP_MAC2ID_I;
MAP_MAC2ID map_Mac2ID;
Ptr<QdPropagationLossModel> lossModelRaytracing;                         //!< Q-D Channel Tracing Model.

/* Type definitions */
struct CommunicationPair
{
  Ptr<Application> srcApp;
  Ptr<PacketSink> packetSink;
  uint64_t totalRx = 0;
  double throughput = 0;
  Time startTime;
};

struct Parameters : public SimpleRefCount<Parameters>
{
  uint32_t srcNodeID;
  Ptr<DmgWifiMac> wifiMac;
};

typedef std::map<Ptr<Node>, CommunicationPair> CommunicationPairList;
typedef CommunicationPairList::iterator CommunicationPairList_I;
typedef CommunicationPairList::const_iterator CommunicationPairList_CI;

/** Simulation Arguments **/
string applicationType = "onoff";             /* Type of the Tx application */
string socketType = "ns3::UdpSocketFactory";  /* Socket Type (TCP/UDP) */
uint32_t packetSize = 1448;                   /* Application payload size in bytes. */
string dataRate = "300Mbps";                  /* Application data rate. */
string tcpVariant = "NewReno";                /* TCP Variant Type. */
uint32_t maxPackets = 0;                      /* Maximum Number of Packets */
uint32_t msduAggregationSize = 7935;          /* The maximum aggregation size for A-MSDU in Bytes. */
uint32_t mpduAggregationSize = 262143;        /* The maximum aggregation size for A-MPDU in Bytes. */
double simulationTime = 10;                   /* Simulation time in seconds. */
bool csv = false;                             /* Enable CSV output. */
bool reportDataSnr = true;                    /* Report Data Packets SNR. */

/**  Applications **/
CommunicationPairList communicationPairList;    /* List of communicating devices. */

template <typename T>
std::string to_string_with_precision (const T a_value, const int n = 6)
{
  std::ostringstream out;
  out.precision (n);
  out << std::fixed << a_value;
  return out.str ();
}

double
CalculateSingleStreamThroughput (Ptr<PacketSink> sink, uint64_t &lastTotalRx, double &averageThroughput)
{
  double thr = (sink->GetTotalRx () - lastTotalRx) * (double) 8 / 1e5;     /* Convert Application RX Packets to MBits. */
  lastTotalRx = sink->GetTotalRx ();
  averageThroughput += thr;
  return thr;
}

void
CalculateThroughput (void)
{
  double totalThr = 0;
  double thr;
  if (!csv)
    {
      string duration = to_string_with_precision<double> (Simulator::Now ().GetSeconds () - 0.1, 1)
        + " - " + to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1);
      std::cout << std::left << std::setw (12) << duration;
      for (CommunicationPairList_I it = communicationPairList.begin (); it != communicationPairList.end (); it++)
        {
          thr = CalculateSingleStreamThroughput (it->second.packetSink, it->second.totalRx, it->second.throughput);
          totalThr += thr;
          std::cout << std::left << std::setw (12) << thr;
        }
      std::cout << std::left << std::setw (12) << totalThr << std::endl;
    }
  else
    {
      std::cout << to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1);
      for (CommunicationPairList_I it = communicationPairList.begin (); it != communicationPairList.end (); it++)
        {
          thr = CalculateSingleStreamThroughput (it->second.packetSink, it->second.totalRx, it->second.throughput);
          totalThr += thr;
          std::cout << "," << thr;
        }
      std::cout << "," << totalThr << std::endl;
    }
  Simulator::Schedule (MilliSeconds (100), &CalculateThroughput);
}

void
StationAssoicated (Ptr<Node> node, Ptr<DmgWifiMac> staWifiMac, Mac48Address address, uint16_t aid)
{
  if (!csv)
    {
      std::cout << "DMG STA " << staWifiMac->GetAddress () << " associated with DMG PCP/AP " << address
                << ", Association ID (AID) = " << aid << std::endl;
    }
  CommunicationPairList_I it = communicationPairList.find (node);
  if (it != communicationPairList.end ())
    {
      it->second.startTime = Simulator::Now ();
      it->second.srcApp->StartApplication ();
    }
  else
    {
      NS_FATAL_ERROR ("Could not find application to run.");
    }
}

void
StationDeassoicated (Ptr<Node> node, Ptr<DmgWifiMac> staWifiMac, Mac48Address address)
{
  if (!csv)
    {
      std::cout << "DMG STA " << staWifiMac->GetAddress () << " deassociated from DMG PCP/AP " << address << std::endl;
    }
  CommunicationPairList_I it = communicationPairList.find (node);
  if (it != communicationPairList.end ())
    {
      it->second.srcApp->StopApplication ();
    }
  else
    {
      NS_FATAL_ERROR ("Could not find application to delete.");
    }
}

CommunicationPair
InstallApplications (Ptr<Node> srcNode, Ptr<Node> dstNode, Ipv4Address address, uint8_t appNumber)
{
  CommunicationPair commPair;

  /* Install TCP/UDP Transmitter on the source node */
  Address dest (InetSocketAddress (address, 9000 + appNumber));
  ApplicationContainer srcApp;
  if (applicationType == "onoff")
    {
      OnOffHelper src (socketType, dest);
      src.SetAttribute ("MaxBytes", UintegerValue (maxPackets));
      src.SetAttribute ("PacketSize", UintegerValue (packetSize));
      src.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1e6]"));
      src.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      src.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate)));
      srcApp = src.Install (srcNode);
    }
  else if (applicationType == "bulk")
    {
      BulkSendHelper src (socketType, dest);
      srcApp = src.Install (srcNode);
    }
  srcApp.Start (Seconds (simulationTime + 1));
  srcApp.Stop (Seconds (simulationTime));
  commPair.srcApp = srcApp.Get (0);

  /* Install Simple TCP/UDP Server on the destination node */
  PacketSinkHelper sinkHelper (socketType, InetSocketAddress (Ipv4Address::GetAny (), 9000 + appNumber));
  ApplicationContainer sinkApp = sinkHelper.Install (dstNode);
  commPair.packetSink = StaticCast<PacketSink> (sinkApp.Get (0));
  sinkApp.Start (Seconds (0.0));

  return commPair;
}

void
SLSCompleted (Ptr<OutputStreamWrapper> stream, Ptr<Parameters> parameters,
              Mac48Address address, ChannelAccessPeriod accessPeriod,
              BeamformingDirection beamformingDirection, bool isInitiatorTxss, bool isResponderTxss,
              SECTOR_ID sectorId, ANTENNA_ID antennaId)
{
  // In the Visualizer, node ID takes into account the AP so +1 is needed
  *stream->GetStream () << parameters->srcNodeID + 1 << "," << map_Mac2ID[address] + 1 << ","
                        << lossModelRaytracing->GetCurrentTraceIndex () << ","
                        << uint16_t (sectorId) << "," << uint16_t (antennaId)  << ","
                        << parameters->wifiMac->GetTypeOfStation ()  << ","
                        << map_Mac2ID[parameters->wifiMac->GetBssid ()] + 1  << ","
                        << Simulator::Now ().GetNanoSeconds () << std::endl;
  if (!csv)
    {
      std::cout << "DMG STA: " << parameters->srcNodeID << " Address: " << address << " Sector ID: " << uint16_t (sectorId)
                << " Antenna ID: " << uint16_t (antennaId) << std::endl;
    }
}

/*** Beamforming CBAP ***/
uint16_t biThreshold = 10;                                    /* BI Threshold to trigger TxSS TXOP. */
std::map<Mac48Address, uint16_t> biCounter;                   /* Number of beacon intervals that have passed. */

void
DataTransmissionIntervalStarted (Ptr<DmgStaWifiMac> wifiMac, Mac48Address address, Time)
{
  if (wifiMac->IsAssociated ())
    {
      uint16_t counter = biCounter[address];
      counter++;
      if (counter == biThreshold)
        {
          wifiMac->InitiateTxssCbap (wifiMac->GetBssid ());
          counter = 0;
        }
      biCounter[address] = counter;
    }
}

void
MacRxOk (Ptr<DmgWifiMac> WifiMac, Ptr<OutputStreamWrapper> stream,
         WifiMacType type, Mac48Address address, double snrValue)
{
  if ((type == WIFI_MAC_QOSDATA) && reportDataSnr)
    {
      *stream->GetStream () << Simulator::Now ().GetNanoSeconds () << ","
                            << address << ","
                            << WifiMac->GetAddress () << ","
                            << snrValue << std::endl;
    }
  else if ((type == WIFI_MAC_EXTENSION_DMG_BEACON) || (type == WIFI_MAC_CTL_DMG_SSW)
           || (type == WIFI_MAC_CTL_DMG_SSW_FBCK) || (type == WIFI_MAC_CTL_DMG_SSW_ACK))
    {
      *stream->GetStream () << Simulator::Now ().GetNanoSeconds () << ","
                            << address << ","
                            << WifiMac->GetAddress () << ","
                            << snrValue << std::endl;
    }
}

int
main (int argc, char *argv[])
{
  uint32_t bufferSize = 131072;                   /* TCP Send/Receive Buffer Size. */
  uint32_t queueSize = 1000;                      /* Wifi MAC Queue Size. */
  bool frameCapture = false;                      /* Use a frame capture model. */
  double frameCaptureMargin = 10;                 /* Frame capture margin in dB. */
  string phyMode = "DMG_MCS12";                   /* Type of the Physical Layer. */
  uint32_t snapShotLength = std::numeric_limits<uint32_t>::max (); /* The maximum PCAP Snapshot Length */
  bool verbose = false;                           /* Print Logging Information. */
  bool pcapTracing = false;                       /* PCAP Tracing is enabled or not. */
  uint16_t numSTAs = 10;                          /* The number of DMG STAs. */
  std::map<std::string, std::string> tcpVariants; /* List of the TCP Variants */
  std::string qdChannelFolder = "DenseScenario"; /* The name of the folder containing the QD-Channel files. */

  /** TCP Variants **/
  tcpVariants.insert (std::make_pair ("NewReno",       "ns3::TcpNewReno"));
  tcpVariants.insert (std::make_pair ("Hybla",         "ns3::TcpHybla"));
  tcpVariants.insert (std::make_pair ("HighSpeed",     "ns3::TcpHighSpeed"));
  tcpVariants.insert (std::make_pair ("Vegas",         "ns3::TcpVegas"));
  tcpVariants.insert (std::make_pair ("Scalable",      "ns3::TcpScalable"));
  tcpVariants.insert (std::make_pair ("Veno",          "ns3::TcpVeno"));
  tcpVariants.insert (std::make_pair ("Bic",           "ns3::TcpBic"));
  tcpVariants.insert (std::make_pair ("Westwood",      "ns3::TcpWestwood"));
  tcpVariants.insert (std::make_pair ("WestwoodPlus",  "ns3::TcpWestwoodPlus"));

  /* Command line argument parser setup. */
  CommandLine cmd;
  cmd.AddValue ("applicationType", "Type of the Tx Application: onoff or bulk", applicationType);
  cmd.AddValue ("packetSize", "Application packet size in bytes", packetSize);
  cmd.AddValue ("dataRate", "Application data rate", dataRate);
  cmd.AddValue ("maxPackets", "Maximum number of packets to send", maxPackets);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpTahoe, TcpReno, TcpNewReno, TcpWestwood, TcpWestwoodPlus", tcpVariant);
  cmd.AddValue ("socketType", "Type of the Socket (ns3::TcpSocketFactory, ns3::UdpSocketFactory)", socketType);
  cmd.AddValue ("bufferSize", "TCP Buffer Size (Send/Receive) in Bytes", bufferSize);
  cmd.AddValue ("msduAggregation", "The maximum aggregation size for A-MSDU in Bytes", msduAggregationSize);
  cmd.AddValue ("mpduAggregation", "The maximum aggregation size for A-MPDU in Bytes", mpduAggregationSize);
  cmd.AddValue ("queueSize", "The maximum size of the Wifi MAC Queue", queueSize);
  cmd.AddValue ("frameCapture", "Use a frame capture model", frameCapture);
  cmd.AddValue ("frameCaptureMargin", "Frame capture model margin in dB", frameCaptureMargin);
  cmd.AddValue ("phyMode", "802.11ad PHY Mode", phyMode);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("reportDataSnr", "Report SNR for data packets = True or for BF Control Packets = False", reportDataSnr);
  cmd.AddValue ("snapShotLength", "The maximum PCAP Snapshot Length", snapShotLength);
  cmd.AddValue ("qdChannelFolder", "The name of the folder containing the QD-Channel files", qdChannelFolder);
  cmd.AddValue ("numSTAs", "The number of DMG STA", numSTAs);
  cmd.AddValue ("pcap", "Enable PCAP Tracing", pcapTracing);
  cmd.AddValue ("csv", "Enable CSV output instead of plain text. This mode will suppress all the messages related statistics and events.", csv);
  cmd.Parse (argc, argv);

  /* Global params: no fragmentation, no RTS/CTS, fixed rate for all packets */
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::QueueBase::MaxPackets", UintegerValue (queueSize));

  /*** Configure TCP Options ***/
  /* Select TCP variant */
  std::map<std::string, std::string>::const_iterator iter = tcpVariants.find (tcpVariant);
  NS_ASSERT_MSG (iter != tcpVariants.end (), "Cannot find Tcp Variant");
  TypeId tid = TypeId::LookupByName (iter->second);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (tid));
  if (tcpVariant.compare ("Westwood") == 0)
    {
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOOD));
      Config::SetDefault ("ns3::TcpWestwood::FilterType", EnumValue (TcpWestwood::TUSTIN));
    }
  else if (tcpVariant.compare ("WestwoodPlus") == 0)
    {
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
      Config::SetDefault ("ns3::TcpWestwood::FilterType", EnumValue (TcpWestwood::TUSTIN));
    }

  /* Configure TCP Segment Size */
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (packetSize));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (bufferSize));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (bufferSize));

  /**** Set up Channel ****/
  Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
  Ptr<QdPropagationDelay> propagationDelayRayTracing = CreateObject<QdPropagationDelay> ();
  lossModelRaytracing  = CreateObject<QdPropagationLossModel> ();
  lossModelRaytracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/" + qdChannelFolder + "/"));
  propagationDelayRayTracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/" + qdChannelFolder + "/"));
  spectrumChannel->AddSpectrumPropagationLossModel (lossModelRaytracing);
  spectrumChannel->SetPropagationDelayModel (propagationDelayRayTracing);

  /**** Setup physical layer ****/
  SpectrumDmgWifiPhyHelper spectrumWifiPhy = SpectrumDmgWifiPhyHelper::Default ();
  spectrumWifiPhy.SetChannel (spectrumChannel);
  /* All nodes transmit at 10 dBm == 10 mW, no adaptation */
  spectrumWifiPhy.Set ("TxPowerStart", DoubleValue (10.0));
  spectrumWifiPhy.Set ("TxPowerEnd", DoubleValue (10.0));
  spectrumWifiPhy.Set ("TxPowerLevels", UintegerValue (1));
  if (frameCapture)
    {
      /* Frame Capture Model */
      spectrumWifiPhy.Set ("FrameCaptureModel", StringValue ("ns3::SimpleFrameCaptureModel"));
      Config::SetDefault ("ns3::SimpleFrameCaptureModel::Margin", DoubleValue (frameCaptureMargin));
    }
  /* Set operating channel */
  spectrumWifiPhy.Set ("ChannelNumber", UintegerValue (2));
  /* Set error model */
  spectrumWifiPhy.SetErrorRateModel ("ns3::DmgErrorModel",
                                     "FileName", StringValue ("DmgFiles/ErrorModel/LookupTable_1458.txt"));
  /* Sensitivity model includes implementation loss and noise figure */
  spectrumWifiPhy.Set ("CcaMode1Threshold", DoubleValue (-79));
  spectrumWifiPhy.Set ("EnergyDetectionThreshold", DoubleValue (-79 + 3));

  /* Create 1 DMG PCP/AP */
  NodeContainer apWifiNode;
  apWifiNode.Create (1);
  /* Create 10 DMG STAs */
  NodeContainer staWifiNodes;
  staWifiNodes.Create (numSTAs);

  /**** WifiHelper is a meta-helper: it helps creates helpers ****/
  DmgWifiHelper wifi;

  /* Add a DMG upper mac */
  DmgWifiMacHelper wifiMacHelper = DmgWifiMacHelper::Default ();

  Ssid ssid = Ssid ("DenseScenario");
  wifiMacHelper.SetType ("ns3::DmgApWifiMac",
                         "Ssid", SsidValue (ssid),
                         "BE_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                         "SSSlotsPerABFT", UintegerValue (8), "SSFramesPerSlot", UintegerValue (13),
                         "BeaconInterval", TimeValue (MicroSeconds (102400)),
                         "ATIPresent", BooleanValue (false));

  /* Set Parametric Codebook for the DMG AP */
  wifi.SetCodebook ("ns3::CodebookParametric",
                    "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_AP_28x.txt"));

  /* Create Wifi Network Devices (WifiNetDevice) */
  NetDeviceContainer apDevice;
  apDevice = wifi.Install (spectrumWifiPhy, wifiMacHelper, apWifiNode);

  wifiMacHelper.SetType ("ns3::DmgStaWifiMac",
                         "BE_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                         "Ssid", SsidValue (ssid), "ActiveProbing", BooleanValue (false));

  /* Set Parametric Codebook for the DMG STA */
  wifi.SetCodebook ("ns3::CodebookParametric",
                    "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_STA_28x.txt"));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (spectrumWifiPhy, wifiMacHelper, staWifiNodes);

  /* MAP MAC Addresses to NodeIDs */
  NetDeviceContainer devices;
  Ptr<WifiNetDevice> netDevice;
  devices.Add (apDevice);
  devices.Add (staDevices);
  for (uint32_t i = 0; i < devices.GetN (); i++)
    {
      netDevice = StaticCast<WifiNetDevice> (devices.Get (i));
      map_Mac2ID[netDevice->GetMac ()->GetAddress ()] = netDevice->GetNode ()->GetId ();
    }

  /* Setting mobility model for AP */
  MobilityHelper mobilityAp;
  mobilityAp.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobilityAp.Install (apWifiNode);

  /* Setting mobility model for STA */
  MobilityHelper mobilitySta;
  mobilitySta.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobilitySta.Install (staWifiNodes);

  /* Internet stack*/
  InternetStackHelper stack;
  stack.Install (apWifiNode);
  stack.Install (staWifiNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface;
  apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces;
  staInterfaces = address.Assign (staDevices);

  /* We do not want any ARP packets */
  PopulateArpCache ();

  /** Install Applications **/
  /* DMG STA -->  DMG AP */
  for (uint32_t i = 0; i < staWifiNodes.GetN (); i++)
    {
      communicationPairList[staWifiNodes.Get (i)] = InstallApplications (staWifiNodes.Get (i), apWifiNode.Get (0),
                                                                         apInterface.GetAddress (0), i);
    }

  /* Print Traces */
  if (pcapTracing)
    {
      spectrumWifiPhy.SetPcapDataLinkType (SpectrumWifiPhyHelper::DLT_IEEE802_11_RADIO);
      spectrumWifiPhy.EnablePcap ("Traces/AccessPoint", apDevice, false);
      spectrumWifiPhy.EnablePcap ("Traces/STA", staDevices, false);
    }

  /* Callback for DMG STAs SLS */
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> outputSlsPhase = ascii.CreateFileStream ("slsResults.csv");
  *outputSlsPhase->GetStream () << "SRC_ID,DST_ID,TRACE_IDX,SECTOR_ID,ANTENNA_ID,ROLE,BSS_ID,Timestamp" << std::endl;

  /* Get SNR Traces */
  Ptr<OutputStreamWrapper> snrStream = ascii.CreateFileStream ("snrValues.csv");
  *snrStream->GetStream () << "TIME,SRC,DST,SNR" << std::endl;

  Ptr<WifiNetDevice> wifiNetDevice;
  Ptr<DmgApWifiMac> apWifiMac;
  Ptr<DmgStaWifiMac> staWifiMac;
  Ptr<WifiRemoteStationManager> remoteStationManager;

  /* Connect DMG STA traces */
  for (uint32_t i = 0; i < staDevices.GetN (); i++)
    {
      wifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (i));
      staWifiMac = StaticCast<DmgStaWifiMac> (wifiNetDevice->GetMac ());
      remoteStationManager = wifiNetDevice->GetRemoteStationManager ();
      remoteStationManager->TraceConnectWithoutContext ("MacRxOK", MakeBoundCallback (&MacRxOk, staWifiMac, snrStream));
      staWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssoicated, staWifiNodes.Get (i), staWifiMac));
      staWifiMac->TraceConnectWithoutContext ("DeAssoc", MakeBoundCallback (&StationDeassoicated, staWifiNodes.Get (i), staWifiMac));

      Ptr<Parameters> parameters = Create<Parameters> ();
      parameters->srcNodeID = wifiNetDevice->GetNode ()->GetId ();
      parameters->wifiMac = staWifiMac;
      staWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parameters));
      staWifiMac->TraceConnectWithoutContext ("DTIStarted", MakeBoundCallback (&DataTransmissionIntervalStarted, staWifiMac));
      biCounter[staWifiMac->GetAddress ()] = 0;
    }

  /* Connect DMG PCP/AP trace */
  wifiNetDevice = StaticCast<WifiNetDevice> (apDevice.Get (0));
  apWifiMac = StaticCast<DmgApWifiMac> (wifiNetDevice->GetMac ());
  remoteStationManager = wifiNetDevice->GetRemoteStationManager ();
  Ptr<Parameters> parameters = Create<Parameters> ();
  parameters->srcNodeID = wifiNetDevice->GetNode ()->GetId ();
  parameters->wifiMac = apWifiMac;
  apWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parameters));
  remoteStationManager->TraceConnectWithoutContext ("MacRxOK", MakeBoundCallback (&MacRxOk, apWifiMac, snrStream));

  /* Enable Traces */
  if (pcapTracing)
    {
      spectrumWifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);
      spectrumWifiPhy.SetSnapshotLength (snapShotLength);
      spectrumWifiPhy.EnablePcap ("Traces/AccessPoint", apDevice, false);
      spectrumWifiPhy.EnablePcap ("Traces/STA", staDevices, false);
    }

  /* Install FlowMonitor on all nodes */
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  /* Print Output */
  if (!csv)
    {
      std::cout << "Application Layer Throughput per Communicating Pair [Mbps]" << std::endl;
      std::cout << std::left << std::setw (12) << "Time [s]";
      string columnName;
      for (uint8_t i = 0; i < communicationPairList.size (); i++)
        {
          columnName = "Pair (" + std::to_string (i + 1) + ")";
          std::cout << std::left << std::setw (12) << columnName;
        }
      std::cout << std::left << std::setw (12) << "Total" << std::endl;
    }

  /* Schedule Throughput Calulcations */
  Simulator::Schedule (Seconds (0.1), &CalculateThroughput);

  Simulator::Stop (Seconds (simulationTime + 0.101));
  Simulator::Run ();
  Simulator::Destroy ();

  if (!csv)
    {
      /* Print per flow statistics */
      monitor->CheckForLostPackets ();
      Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
      FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
      for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
        {
          Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
          std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
          std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
          std::cout << "  Tx Bytes:   " << i->second.txBytes << std::endl;
          std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / ((simulationTime - 0.1) * 1e6)  << " Mbps" << std::endl;
          std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
          std::cout << "  Rx Bytes:   " << i->second.rxBytes << std::endl;
          std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / ((simulationTime - 0.1) * 1e6)  << " Mbps" << std::endl;
        }

      /* Print Application Layer Results Summary */
      std::cout << "\nApplication Layer Statistics:" << std::endl;
      Ptr<OnOffApplication> onoff;
      Ptr<BulkSendApplication> bulk;
      uint16_t communicationLinks = 1;
      for (CommunicationPairList_I it = communicationPairList.begin (); it != communicationPairList.end (); it++)
        {
          std::cout << "Communication Link (" << communicationLinks << ") Statistics:" << std::endl;
          if (applicationType == "onoff")
            {
              onoff = StaticCast<OnOffApplication> (it->second.srcApp);
              std::cout << "  Tx Packets: " << onoff->GetTotalTxPackets () << std::endl;
              std::cout << "  Tx Bytes:   " << onoff->GetTotalTxBytes () << std::endl;
            }
          else
            {
              bulk = StaticCast<BulkSendApplication> (it->second.srcApp);
              std::cout << "  Tx Packets: " << bulk->GetTotalTxPackets () << std::endl;
              std::cout << "  Tx Bytes:   " << bulk->GetTotalTxBytes () << std::endl;
            }
          Ptr<PacketSink> packetSink = StaticCast<PacketSink> (it->second.packetSink);
          std::cout << "  Rx Packets: " << packetSink->GetTotalReceivedPackets () << std::endl;
          std::cout << "  Rx Bytes:   " << packetSink->GetTotalRx () << std::endl;
          std::cout << "  Throughput: " << packetSink->GetTotalRx () * 8.0 / ((simulationTime - it->second.startTime.GetSeconds ()) * 1e6)
                    << " Mbps" << std::endl;
          communicationLinks++;
        }
    }

  return 0;
}
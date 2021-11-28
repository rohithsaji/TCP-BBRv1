/*
Original file : /examples/tcp/tcp-bbr-example
File modifed to trace goodput, with a wireless AD-HOC bottleneck.



Topology:
                                      * 24Mbps *
                         100Mbps/5ms  |        |   100Mbps/5ms
                      n0 ------------ n1       n2 ------------n4
                    Source                               Destination 
*/

#include "ns3/netanim-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-helper.h"
#include <iostream>
using namespace ns3;

std::string dir;
uint32_t prev = 0;
Time prevTime = Seconds (0);

// Calculate Goodput : Recieved Bytes are used for goodput calculation, as Tcp does not recieve duplicate packets

static void
TraceThroughput (Ptr<FlowMonitor> monitor)
{
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  auto itr = stats.begin ();
  Time curTime = Now ();
  std::ofstream thr (dir + "/goodputput.dat", std::ios::out | std::ios::app);
  thr <<  curTime.GetSeconds ()  << " " << 8 * (itr->second.rxBytes - prev) / (1000 * 1000 * (curTime.GetSeconds () - prevTime.GetSeconds ())) << std::endl;
  std::cout<<"Time:"<<curTime.GetSeconds()<<"GP: "<<8 * (itr->second.rxBytes - prev) / (1000 * 1000 * (curTime.GetSeconds () - prevTime.GetSeconds ()))<<std::endl;
  prevTime = curTime;
  prev = itr->second.rxBytes;
  Simulator::Schedule (Seconds (0.2), &TraceThroughput, monitor);
}

// Check the queue size
void CheckQueueSize (Ptr<QueueDisc> qd)
{
  uint32_t qsize = qd->GetCurrentSize ().GetValue ();
  Simulator::Schedule (Seconds (0.2), &CheckQueueSize, qd);
  std::ofstream q (dir + "/queueSize.dat", std::ios::out | std::ios::app);
  q << Simulator::Now ().GetSeconds () << " " << qsize << std::endl;
  q.close ();
}

// Trace congestion window
static void CwndTracer (Ptr<OutputStreamWrapper> stream, uint32_t oldval, uint32_t newval)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval / 1448.0 << std::endl;
}

void TraceCwnd (uint32_t nodeId, uint32_t socketId)
{
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream (dir + "/cwnd.dat");
  Config::ConnectWithoutContext ("/NodeList/" + std::to_string (nodeId) + "/$ns3::TcpL4Protocol/SocketList/" + std::to_string (socketId) + "/CongestionWindow", MakeBoundCallback (&CwndTracer, stream));
}

int main (int argc, char *argv [])
{
  // Naming the output directory using local system time
  time_t rawtime;
  struct tm * timeinfo;
  char buffer [80];
  time (&rawtime);
  timeinfo = localtime (&rawtime);
  strftime (buffer, sizeof (buffer), "%d-%m-%Y-%I-%M-%S", timeinfo);
  std::string currentTime (buffer);
  double error=0.000001;
  std::string tcpTypeId = "TcpBbr";
  std::string queueDisc = "FifoQueueDisc";
  uint32_t delAckCount = 2;
  uint32_t datarate = 100;

  bool bql = true;
  Time stopTime = Seconds (10);

  CommandLine cmd (__FILE__);
  cmd.AddValue ("tcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", tcpTypeId);
  cmd.AddValue ("datarate", "Datarate of the edge links", datarate);
  cmd.AddValue ("error", "Enter receiver error rate", error);
  cmd.AddValue ("delAckCount", "Delayed ACK count", delAckCount);
  cmd.Parse (argc, argv);

  queueDisc = std::string ("ns3::") + queueDisc;

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (41943040));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (62914560));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (delAckCount));
  Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p")));
  Config::SetDefault (queueDisc + "::MaxSize", QueueSizeValue (QueueSize ("100p")));

  NodeContainer sender, receiver;
  NodeContainer Wifinodes;
  sender.Create (1);
  receiver.Create (1);
  Wifinodes.Create (2);



  PointToPointHelper edgeLink;
  edgeLink.SetDeviceAttribute  ("DataRate", StringValue ("100Mbps"));
  edgeLink.SetChannelAttribute ("Delay", StringValue ("5ms"));

  // Create NetDevice containers
  NetDeviceContainer senderEdge = edgeLink.Install (sender.Get (0), Wifinodes.Get (0));

  NetDeviceContainer receiverEdge = edgeLink.Install (Wifinodes.Get (1), receiver.Get (0));

  YansWifiChannelHelper channel;
  channel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  channel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
  YansWifiPhyHelper phy;
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> (); 
  em->SetAttribute ("ErrorRate", DoubleValue (error));
  phy.Set ("PostReceptionErrorModel",PointerValue(em));
  phy.SetChannel (channel.Create ());

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211a);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate24Mbps"),
                                "ControlMode", StringValue ("OfdmRate24Mbps"));

  WifiMacHelper mac;
  mac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer rightNetDevices;
  rightNetDevices = wifi.Install (phy, mac, Wifinodes);

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (Wifinodes);

    // Install Stack
  InternetStackHelper internet;
  internet.Install (sender);
  internet.Install (receiver);
  internet.Install (Wifinodes);

  // Configure the root queue discipline
  TrafficControlHelper tch;
  tch.SetRootQueueDisc (queueDisc);

  if (bql)
    {
      tch.SetQueueLimits ("ns3::DynamicQueueLimits", "HoldTime", StringValue ("1000ms"));
    }

  tch.Install (senderEdge);
  tch.Install (receiverEdge);

  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.0.0.0", "255.255.255.0");

  Ipv4InterfaceContainer i1i2 = ipv4.Assign (rightNetDevices);

  ipv4.NewNetwork ();
  Ipv4InterfaceContainer is1 = ipv4.Assign (senderEdge);

  ipv4.NewNetwork ();
  Ipv4InterfaceContainer ir1 = ipv4.Assign (receiverEdge);

  // Populate routing tables
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Select sender side port
  uint16_t port = 50001;

  // Install application on the sender
  BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (ir1.GetAddress (1), port));
  source.SetAttribute ("MaxBytes", UintegerValue (0));
  ApplicationContainer sourceApps = source.Install (sender.Get (0));
  sourceApps.Start (Seconds (0.0));
  Simulator::Schedule (Seconds (0.2), &TraceCwnd, 0, 0);
  sourceApps.Stop (stopTime);

  // Install application on the receiver
  PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sink.Install (receiver.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (stopTime);

  // Create a new directory to store the output of the program
  dir = "goodput-results/" + currentTime + "/";
  std::string dirToSave = "mkdir -p " + dir;
  if (system (dirToSave.c_str ()) == -1)
    {
      exit (1);
    }

  // Trace the queue occupancy on the second interface of R1
  tch.Uninstall (Wifinodes.Get (0)->GetDevice (1));
  QueueDiscContainer qd;
  qd = tch.Install (Wifinodes.Get (0)->GetDevice (1));
  Simulator::ScheduleNow (&CheckQueueSize, qd.Get (0));

 
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  Simulator::Schedule (Seconds (0 + 0.000001), &TraceThroughput, monitor);

  Simulator::Stop (stopTime + TimeStep (1));
  Simulator::Run ();
  Simulator::Destroy ();
 
  double averageGood = double(8 * (prev)) / double(1000 * 1000 * (10));
  std::cout << "\nAverage Goodput: " << averageGood << " Mbit/s" ;

  return 0;
}

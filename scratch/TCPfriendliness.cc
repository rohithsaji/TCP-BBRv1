#include <iostream>

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/gtk-config-store.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("friendlinesseval");

uint32_t    nleftLeaf = 4;
uint32_t    nrightLeaf = 4;

void ThroughputMonitor (Ptr<FlowMonitor> flowMon)
{
  double fairnessIndex = 0, xSum = 0, x2Sum = 0;

  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMon->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      double Throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024;
      if (i->first <= nleftLeaf)
        {
          xSum = xSum + Throughput;
          x2Sum = x2Sum + Throughput * Throughput;
        }
    }

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      double Throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024;
      if (i->first <= nleftLeaf)
        {
          std::cout << "Fairness Index " << i->first << " : " << Throughput / xSum << "\n";
        }
    }

  fairnessIndex = xSum * xSum / (x2Sum * nleftLeaf);
  std::cout << "Fairness Index: " << fairnessIndex << "\n";
}

int main (int argc, char *argv[])
{
  uint16_t port = 50000;
  double stopTime = 300;
  uint32_t    maxWindowSize = 2000;
  bool isWindowScalingEnabled = true;
  std::string leftRate = "150Mbps";
  std::string leftDelay = "10ms";
  std::string rightRate = "20Mbps";
  std::string rightDelay = "1ms";
  std::string middleRate = "100Mbps";
  std::string middleDelay = "45ms";
  std::string TcpCubic = "ns3::Cubic";
  std::string mobilityModel = "ns3::ConstantPositionMobilityModel";

  CommandLine cmd;
  cmd.Parse (argc, argv);

  /**
   * Setting the TCP window size to be used
   * Setting the window scaling option
   * Setting TCP variant to be used
   */
  Config::SetDefault ("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (maxWindowSize));
  Config::SetDefault ("ns3::TcpSocketBase::WindowScaling", BooleanValue (isWindowScalingEnabled));

  Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
  Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (200));
  Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (50));
  Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (50));
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (0.2));
  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (true));
  Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));

  PointToPointHelper left;
  left.SetDeviceAttribute  ("DataRate", StringValue (leftRate));
  left.SetChannelAttribute ("Delay", StringValue (leftDelay));


  PointToPointHelper right;
  right.SetDeviceAttribute    ("DataRate", StringValue (rightRate));
  right.SetChannelAttribute   ("Delay", StringValue (rightDelay));

  PointToPointHelper middle;
  middle.SetDeviceAttribute    ("DataRate", StringValue (middleRate));
  middle.SetChannelAttribute   ("Delay", StringValue (middleDelay));

  /**
   * Creating dumbbell topology which uses point to point channel for edge links and bottleneck using PointToPointDumbbellHelper
   */
  WirelessJerseyHelper d (nleftLeaf, nrightLeaf, left, right, middle, mobilityModel);

  InternetStackHelper stack;
  d.InstallStack (stack);

  TrafficControlHelper tchBottleneck;
  QueueDiscContainer queueDiscsLeft;
  QueueDiscContainer queueDiscsMiddle;
  QueueDiscContainer queueDiscsRight;
  tchBottleneck.SetRootQueueDisc ("ns3::RedQueueDisc");
  queueDiscsLeft = tchBottleneck.Install (d.GetLeft ()->GetDevice (0));
  queueDiscsMiddle = tchBottleneck.Install (d.GetMiddle ()->GetDevice (1));
  queueDiscsRight = tchBottleneck.Install (d.GetRight ()->GetDevice (0));

  // Assign IP Addresses
  d.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Config::Set ("$ns3::NodeListPriv/NodeList/" + std::to_string (3) + "/$ns3::TcpL4Protocol/SocketType", TypeIdValue (TypeId::LookupByName (TcpCubic)));

  // Configure application
  for (uint16_t i = 0; i < d.LeftCount (); i++)
    {
      BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
      AddressValue remoteAddress (InetSocketAddress (d.GetRightIpv4Address (i), port));
      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("SendSize", UintegerValue (1000));

      ApplicationContainer sourceApp = ftp.Install (d.GetLeft (i));
      sourceApp.Start (Seconds (0));
      sourceApp.Stop (Seconds (stopTime - 1));

      Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
      sinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));

      ApplicationContainer sinkApp = sinkHelper.Install (d.GetRight (i));
      sinkApp.Start (Seconds (0));
      sinkApp.Stop (Seconds (stopTime));
    }

  std::cout << "Running the simulation" << std::endl;

  FlowMonitorHelper fmHelper;
  Ptr<FlowMonitor> allMon = fmHelper.InstallAll ();
  Simulator::Schedule (Seconds (stopTime + 1),&ThroughputMonitor, allMon);

  Simulator::Stop (Seconds (stopTime + 2));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}

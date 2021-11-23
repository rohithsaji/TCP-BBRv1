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
#include <iomanip>
using namespace ns3;



std::ofstream rxS2R2Throughput;
std::ofstream fairnessIndex;
std::vector<uint64_t> rxS2R2Bytes;

void 
PrintProgress (Time interval)
 {
   std::cout << "Progress to " << std::fixed << std::setprecision(1) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
   Simulator::Schedule (interval, &PrintProgress, interval);
 }

 void TraceS2R2Sink (std::size_t index, Ptr<const Packet> p, const Address& a)
 {
   rxS2R2Bytes[index] += p->GetSize ();
 }


 void
 InitializeCounters (void)
 {

   for (std::size_t i = 0; i < 20; i++)
     {
       rxS2R2Bytes[i] = 0;
     }
 }

  void
 PrintThroughput (Time measurementWindow)
 {
   for (std::size_t i = 0; i < 20; i++)
     {
       rxS2R2Throughput << Simulator::Now ().GetSeconds () << "s " << i << " " << (rxS2R2Bytes[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6 << std::endl;
     }
 }


PrintFairness (Time measurementWindow)
 {
   double average = 0;
   uint64_t sumSquares = 0;
   uint64_t sum = 0;
   double fairness = 0;
   for (std::size_t i = 0; i < 20; i++)
     {
       sum += rxS2R2Bytes[i];
       sumSquares += (rxS2R2Bytes[i] * rxS2R2Bytes[i]);
     }
   average = ((sum / 20) * 8 / measurementWindow.GetSeconds ()) / 1e6;
   fairness = static_cast<double> (sum * sum) / (20 * sumSquares);
   fairnessIndex << "Average throughput for S2-R2 flows: "
                 << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                 << std::fixed << std::setprecision (3) << fairness << std::endl;

  }

  

int main (int argc, char *argv [])
{
std::string outputFilePath = ".";
   std::string tcpTypeId = "TcpBbr";
   Time flowStartupWindow = Seconds (1);
   Time convergenceTime = Seconds (3);
   Time measurementWindow = Seconds (1);
   bool enableSwitchEcn = true;
   bool bql = true;
   bool enablePcap = false;
   Time progressInterval = MilliSeconds (100);
  
   CommandLine cmd (__FILE__);
   cmd.AddValue ("tcpTypeId", "ns-3 TCP TypeId", tcpTypeId);
   cmd.AddValue ("flowStartupWindow", "startup time window (TCP staggered starts)", flowStartupWindow);
   cmd.AddValue ("convergenceTime", "convergence time", convergenceTime);
   cmd.AddValue ("measurementWindow", "measurement window", measurementWindow);
   cmd.AddValue ("enableSwitchEcn", "enable ECN at switches", enableSwitchEcn);
   cmd.Parse (argc, argv);
  
   Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));
  
   Time startTime = Seconds (0);
   Time stopTime = flowStartupWindow + convergenceTime + measurementWindow;
  
   Time clientStartTime = startTime;
    Config::SetDefault (queueDisc + "::MaxSize", QueueSizeValue (QueueSize ("100p")));


    rxS2R2Bytes.reserve (20);



    NodeContainer wirelessNodes;
    wirelessNodes.Create (2);

    NodeContainer wiredNodes;
    wiredNodes.Create (20);

    NodeContainer rwiredNodes;
    rwiredNodes.Create(20);

    PointToPointHelper leftHelper;
    leftHelper.SetDeviceAttribute    ("DataRate", StringValue ("100Mbps"));
    leftHelper.SetChannelAttribute   ("Delay", StringValue ("10ms"));

    PointToPointHelper rightHelper;
    rightHelper.SetDeviceAttribute    ("DataRate", StringValue ("100Mbps"));
    rightHelper.SetChannelAttribute   ("Delay", StringValue ("10ms"));


    std::vector<NetDeviceContainer> leftNetDevices;
    leftNetDevices.reserve (10);
    std::vector<NetDeviceContainer> rightNetDevices;
    rightNetDevices.reserve (20);

    for (std::size_t i = 0; i < 20; i++)
    {
      Ptr<Node> n1 = wiredNodes.Get (i);
      Ptr<Node> n2 = rwiredNodes.Get (i);
      leftNetDevices.push_back(leftHelper.Install (n1, wirelessNodes.Get (0)));
      rightNetDevices.push_back(rightHelper.Install(wirelessNodes.Get (1),n2));
    }


    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                    "DataMode", StringValue ("DsssRate11Mbps"),
                                    "ControlMode", StringValue ("DsssRate11Mbps"));
    WifiMacHelper mac;
    mac.SetType ("ns3::AdhocWifiMac");

    
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
    YansWifiPhyHelper phy;

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> (); 
    em->SetAttribute ("ErrorRate", DoubleValue (0.000001));
    phy.Set ("PostReceptionErrorModel",PointerValue(em));

    phy.SetChannel (channel.Create ());

  NetDeviceContainer wifiNetDevices;
  wifiNetDevices = wifi.Install (phy, mac, wirelessNodes);

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wirelessNodes);

  InternetStackHelper stack;
  stack.Install (wiredNodes);
  stack.Install (wirelessNodes);
  stack.Install (rwiredNodes);


  TrafficControlHelper tch;
  tch.SetRootQueueDisc (queueDisc);

  if (bql)
    {
      tch.SetQueueLimits ("ns3::DynamicQueueLimits", "HoldTime", StringValue ("1000ms"));
    }

  tch.Install (leftNetDevices);
  tch.Install(wifiNetDevices);
  tch.Install (rightNetDevices);


  Ipv4AddressHelper ipv4;

   std::vector<Ipv4InterfaceContainer> iplnd;
   iplnd.reserve (20);
   std::vector<Ipv4InterfaceContainer> iprnd;
   iprnd.reserve (20);

  ipv4.SetBase ("10.0.0.0", "255.255.255.0");

  Ipv4InterfaceContainer middleInterfaces = ipv4.Assign (wifiNetDevices);
  ipv4.NewNetwork ();
  address.SetBase ("10.1.1.0", "255.255.255.0");
   for (std::size_t i = 0; i < 20; i++)
     {
       iplnd.push_back (address.Assign (leftNetDevices[i]));
       address.NewNetwork ();
     }
   address.SetBase ("10.2.1.0", "255.255.255.0");
   for (std::size_t i = 0; i < 20; i++)
     {
       iprnd.push_back (address.Assign (rightNetDevices[i]));
       address.NewNetwork ();
     }


  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (rightInterfaces.GetAddress (1), port));
  source.SetAttribute ("MaxBytes", UintegerValue (0));

  std::vector<Ptr<PacketSink> > rSinks;
   rSinks.reserve (20);
   for (std::size_t i = 0; i < 20; i++)
     {
       uint16_t port = 50000 + i;
       Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
       PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
       ApplicationContainer sinkApp = sinkHelper.Install (rwiredNodes.Get (i));
       Ptr<PacketSink> packetSink = sinkApp.Get (0)->GetObject<PacketSink> ();
       rSinks.push_back (packetSink);
       sinkApp.Start (startTime);
       sinkApp.Stop (stopTime);
  
       OnOffHelper clientHelper1 ("ns3::TcpSocketFactory", Address ());
       clientHelper1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
       clientHelper1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
       clientHelper1.SetAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
       clientHelper1.SetAttribute ("PacketSize", UintegerValue (1000));
  
       ApplicationContainer clientApps1;
       AddressValue remoteAddress (InetSocketAddress (iprnd[i].GetAddress (0), port));
       clientHelper1.SetAttribute ("Remote", remoteAddress);
       clientApps1.Add (clientHelper1.Install (wiredNodes.Get (i)));
       clientApps1.Start (i * flowStartupWindow / 20  + clientStartTime + MilliSeconds (i * 5));
       clientApps1.Stop (stopTime);
     }

     rxS2R2Throughput.open ("BBR.dat", std::ios::out);
     fairnessIndex.open ("BBR-Fairness.dat", std::ios::out);

      for (std::size_t i = 0; i < 20; i++)
     {
       rSinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceS2R2Sink, i));
     }

   Simulator::Schedule (flowStartupWindow + convergenceTime, &InitializeCounters);
   Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintThroughput, measurementWindow);
   Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintFairness, measurementWindow);
   Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
   Simulator::Stop (stopTime + TimeStep (1));

    Simulator::Run ();
  
   rxS2R2Throughput.close ();
   fairnessIndex.close ();
   Simulator::Destroy ();
   return 0;

}
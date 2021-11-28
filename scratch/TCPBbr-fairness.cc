// For the calculation of the tcp-bbr fairness we have taken the dctcp-example.cc from ns3 https://www.nsnam.org/doxygen/dctcp-example_8cc_source.html
// and edited the topology present in it and made the bottleneck wireless and used the same calculation methods for calculating throughput and fairness using 
// Jain's index measure 
// The give topology in the dctcp is roughly as follows
//
//  S1         S3
//  |           |  (1 Gbps)
//  T1 ------- T2 -- R1
//  |           |  (1 Gbps)
//  S2         R2
//
// The link between switch T1 and T2 is 10 Gbps.  All other
// links are 1 Gbps.  In the SIGCOMM paper, there is a Scorpion switch
// between T1 and T2, but it doesn't contribute another bottleneck.
//
// S1 and S3 each have 10 senders sending to receiver R1 (20 total)
// S2 (20 senders) sends traffic to R2 (20 receivers)
//
// This sets up two bottlenecks: 1) T1 -> T2 interface (30 senders
// using the 10 Gbps link) and 2) T2 -> R1 (20 senders using 1 Gbps link)
// The updated topology is rougly as follows:
//    S------T1- - - - wireless bottleneck - - - -T2-------R
//    T1T2 is defined as T
//    here S and R has 20 senders and 20 recievers respectively with 2Mbps and 10ms 
//    The wireless bottleneck has 54Mbps and 100ms(WIFI_STANDARD_80211a)

#include <iostream>
 #include <iomanip>
  
 #include "ns3/core-module.h"
 #include "ns3/network-module.h"
 #include "ns3/internet-module.h"
 #include "ns3/point-to-point-module.h"
 #include "ns3/applications-module.h"
 #include "ns3/traffic-control-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
  
 using namespace ns3;
  
 std::stringstream filePlotQueue1;
 std::stringstream filePlotQueue2;
 std::ofstream rxSRThroughput;
 std::ofstream fairnessIndex;
 std::ofstream t1QueueLength;
 std::ofstream t2QueueLength;
 std::vector<uint64_t> rxSRBytes;
  
 void
 PrintProgress (Time interval)
 {
   std::cout << "Progress to " << std::fixed << std::setprecision (1) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
   Simulator::Schedule (interval, &PrintProgress, interval);
 }
  

  
 void
 TraceSRSink (std::size_t index, Ptr<const Packet> p, const Address& a)
 {
   rxSRBytes[index] += p->GetSize ();
 }
  
 
 void
 InitializeCounters (void)
 {
 
   for (std::size_t i = 0; i < 20; i++)
     {
       rxSRBytes[i] = 0;
     }
  
 }
  
 void
 PrintThroughput (Time measurementWindow)
 {

   for (std::size_t i = 0; i < 20; i++)
     {
       rxSRThroughput << measurementWindow.GetSeconds () << "s " << i << " " << (rxSRBytes[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6 << std::endl;
     }

 }
  
 // Jain's fairness index:  https://en.wikipedia.org/wiki/Fairness_measure
 void
 PrintFairness (Time measurementWindow)
 {
   double average = 0;
   uint64_t sumSquares = 0;
   uint64_t sum = 0;
   double fairness = 0;

   for (std::size_t i = 0; i < 20; i++)
     {
       sum += rxSRBytes[i];
       sumSquares += (rxSRBytes[i] * rxSRBytes[i]);
     }
   average = ((sum / 20) * 8 / measurementWindow.GetSeconds ()) / 1e6;
   fairness = static_cast<double> (sum * sum) / (20 * sumSquares);
   fairnessIndex << "Average throughput for the topology flows: "
                 << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                 << std::fixed << std::setprecision (3) << fairness << std::endl;

   sum = 0;              
   for (std::size_t i = 0; i < 20; i++)
     {
       sum += rxSRBytes[i];
     }
   fairnessIndex << "Aggregate user-level throughput for flows through Bottleneck: " << static_cast<double> (sum * 8) / 1e6 << " Mbps" << std::endl;

 }
  
 void
 CheckT1QueueSize (Ptr<QueueDisc> queue)
 {
   // 1500 byte packets
   uint32_t qSize = queue->GetNPackets ();
   Time backlog = Seconds (static_cast<double> (qSize * 1000 * 8) /(54*(1e6))); // 10 Gb/s  54Mbps
   // report size in units of packets and ms
   t1QueueLength << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
   // check queue size every 1/100 of a second
   Simulator::Schedule (MilliSeconds (10), &CheckT1QueueSize, queue);
 }
  
 void
 CheckT2QueueSize (Ptr<QueueDisc> queue)
 {
   uint32_t qSize = queue->GetNPackets ();
   Time backlog = Seconds (static_cast<double> (qSize * 1000 * 8) / 2*(1e6)); // 1 Gb/s 10Mbps
   // report size in units of packets and ms
   t2QueueLength << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
   // check queue size every 1/100 of a second
   Simulator::Schedule (MilliSeconds (10), &CheckT2QueueSize, queue);
 }
  
 int main (int argc, char *argv[])
 {
   std::string outputFilePath = ".";
   std::string tcpTypeId = "TcpBbr";
   Time flowStartupWindow = Seconds (1);
   Time convergenceTime = Seconds (3);
   Time measurementWindow = Seconds (1);
   bool enableSwitchEcn = true;
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
  
   rxSRBytes.reserve (20);
  
   NodeContainer S, R, Wifinodes;
   S.Create (20);
   R.Create (20);
   Wifinodes.Create(2);
  
   Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
   Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (2));
   GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  
   // Set default parameters for RED queue disc
   Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (enableSwitchEcn));
   Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", BooleanValue (false));
   Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1000));
   Config::SetDefault ("ns3::RedQueueDisc::MaxSize", QueueSizeValue (QueueSize ("4000p")));
   // DCTCP tracks instantaneous queue length only; so set QW = 1
   Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1));
   Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (20));
   Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (60));
  
   PointToPointHelper pointToPointSR;
   pointToPointSR.SetDeviceAttribute ("DataRate", StringValue ("2Mbps"));
   pointToPointSR.SetChannelAttribute ("Delay", StringValue ("10us"));
  

  
  
  
   std::vector<NetDeviceContainer> ST;
   ST.reserve (20);

   std::vector<NetDeviceContainer> RT;
   RT.reserve (20);

  

   for (std::size_t i = 0; i < 20; i++)
     {
       Ptr<Node> n = S.Get (i);
       ST.push_back (pointToPointSR.Install (n, Wifinodes.Get(0)));
     }

   for (std::size_t i = 0; i < 20; i++)
     {
       Ptr<Node> n = R.Get (i);
       RT.push_back (pointToPointSR.Install (n, Wifinodes.Get(1)));
     }


  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy;
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> (); 
  em->SetAttribute ("ErrorRate", DoubleValue (0.00001));
  phy.Set ("PostReceptionErrorModel",PointerValue(em));
  phy.SetChannel (channel.Create ());
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211a);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate54Mbps"),
                                "ControlMode", StringValue ("OfdmRate54Mbps"));
  
  WifiMacHelper mac;
  mac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer rightNetDevices;
  rightNetDevices = wifi.Install (phy, mac, Wifinodes);

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (Wifinodes);

   InternetStackHelper stack;
   stack.InstallAll ();
  
   TrafficControlHelper tchRed10;
 
   tchRed10.SetRootQueueDisc ("ns3::RedQueueDisc",
                              "LinkBandwidth", StringValue ("54Mbps"),
                              "LinkDelay", StringValue ("100us"),
                              "MinTh", DoubleValue (50),
                              "MaxTh", DoubleValue (150));
   QueueDiscContainer queueDiscs1 = tchRed10.Install (rightNetDevices);
  
   TrafficControlHelper tchRed1;
   tchRed1.SetRootQueueDisc ("ns3::RedQueueDisc",
                             "LinkBandwidth", StringValue ("2Mbps"),
                             "LinkDelay", StringValue ("20us"),
                             "MinTh", DoubleValue (20),
                             "MaxTh", DoubleValue (60));

   for (std::size_t i = 0; i < 20; i++)
     {
       tchRed1.Install (ST[i].Get (1));
     }

   for (std::size_t i = 0; i < 20; i++)
     {
       tchRed1.Install (RT[i].Get (1));
     }
  
   Ipv4AddressHelper address;

   std::vector<Ipv4InterfaceContainer> ipST;
   ipST.reserve (20);

   std::vector<Ipv4InterfaceContainer> ipRT;
   ipRT.reserve (20);
   address.SetBase ("10.0.0.0", "255.255.255.0");
   Ipv4InterfaceContainer ipTT = address.Assign (rightNetDevices);

   address.SetBase ("10.1.1.0", "255.255.255.0");
   for (std::size_t i = 0; i < 20; i++)
     {
       ipST.push_back (address.Assign (ST[i]));
       address.NewNetwork ();
     }

   address.SetBase ("10.2.1.0", "255.255.255.0");
   for (std::size_t i = 0; i < 20; i++)
     {
       ipRT.push_back (address.Assign (RT[i]));
       address.NewNetwork ();
     }
  
   Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  
   // Each sender in S sends to a receiver in R
   std::vector<Ptr<PacketSink> > rSinks;
   rSinks.reserve (20);
   for (std::size_t i = 0; i < 20; i++)
     {
       uint16_t port = 50000 + i;
       Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
       PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
       ApplicationContainer sinkApp = sinkHelper.Install (R.Get (i));
       Ptr<PacketSink> packetSink = sinkApp.Get (0)->GetObject<PacketSink> ();
       rSinks.push_back (packetSink);
       sinkApp.Start (startTime);
       sinkApp.Stop (stopTime);
  
       OnOffHelper clientHelper1 ("ns3::TcpSocketFactory", Address ());
       clientHelper1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
       clientHelper1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
       clientHelper1.SetAttribute ("DataRate", DataRateValue (DataRate ("10Mbps")));
       clientHelper1.SetAttribute ("PacketSize", UintegerValue (1000));
  
       ApplicationContainer clientApps1;
       AddressValue remoteAddress (InetSocketAddress (ipRT[i].GetAddress (0), port));
       clientHelper1.SetAttribute ("Remote", remoteAddress);
       clientApps1.Add (clientHelper1.Install (S.Get (i)));
       clientApps1.Start (i * flowStartupWindow / 20  + clientStartTime + MilliSeconds (i * 5));
       clientApps1.Stop (stopTime);
     }
  
   
   rxSRThroughput.open ("throughput.dat", std::ios::out);
   rxSRThroughput << "#Time(s) flow thruput(Mb/s)" << std::endl;
   fairnessIndex.open ("fairness.dat", std::ios::out);

   for (std::size_t i = 0; i < 20; i++)
     {
       rSinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceSRSink, i));
     }
 
   Simulator::Schedule (flowStartupWindow + convergenceTime, &InitializeCounters);
   Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintThroughput, measurementWindow);
   Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow, &PrintFairness, measurementWindow);
   Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
   Simulator::Schedule (flowStartupWindow + convergenceTime, &CheckT1QueueSize, queueDiscs1.Get (0));
   Simulator::Stop (stopTime + TimeStep (1));
  
   Simulator::Run ();
  
   rxSRThroughput.close ();
   fairnessIndex.close ();
   t1QueueLength.close ();
   Simulator::Destroy ();
   return 0;
 }
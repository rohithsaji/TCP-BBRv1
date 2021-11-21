/*
Modified from examples/tcp/tcp-bbr-exmaple.cc
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

NS_LOG_COMPONENT_DEFINE ("tcpbbr-goodput");


std::string dir;
uint32_t prev = 0;
Time prevTime = Seconds (0);

// Calculate goodput: Recieved Bytes are used for goodput calculation. 

static void
TraceGoodput (Ptr<FlowMonitor> monitor)
{
 
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  auto itr = stats.begin ();
  Time curTime = Now ();
  std::cout<<curTime.GetSeconds ()<<std::endl;
  std::ofstream thr (dir + "/goodput.dat", std::ios::out | std::ios::app);
  thr <<  curTime.GetSeconds () << " " << 8 * (itr->second.rxBytes - prev) / (1000 * 1000 * (curTime.GetSeconds () - prevTime.GetSeconds ())) << std::endl;
  prevTime = curTime;
  prev = itr->second.rxBytes;
  Simulator::Schedule (Seconds (0.2), &TraceGoodput, monitor);
}

// // Check the queue size
void CheckQueueSize (Ptr<QueueDisc> qd)
{
  uint32_t qsize = qd->GetCurrentSize ().GetValue ();
  Simulator::Schedule (Seconds (0.2), &CheckQueueSize, qd);
  std::ofstream q (dir + "/queueSize.dat", std::ios::out | std::ios::app);
  q << Simulator::Now ().GetSeconds () << " " << qsize << std::endl;
  q.close ();
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
    uint16_t port = 50001;
    std::string tcpTypeId = "TcpBbr";
    std::string queueDisc = "FifoQueueDisc";
    uint32_t delAckCount = 2;
    bool bql = true;
    bool enablePcap = false;
    Time stopTime = Seconds (100);
    double errorRate=0;


    CommandLine cmd (__FILE__);
    cmd.AddValue ("tcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", tcpTypeId);
    cmd.AddValue ("enablePcap", "Enable/Disable pcap file generation", enablePcap);
    cmd.AddValue ("stopTime", "Stop time for applications / simulation time will be stopTime + 1", stopTime);
    cmd.AddValue ("errorRate", "Enter error rate for wifi channel", errorRate);
    cmd.Parse (argc, argv);

    queueDisc = std::string ("ns3::") + queueDisc;

    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (4194304));
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (6291456));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (delAckCount));
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
    Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p")));
    Config::SetDefault (queueDisc + "::MaxSize", QueueSizeValue (QueueSize ("100p")));



    NodeContainer wirelessNodes;
    wirelessNodes.Create (2);

    NodeContainer wiredNodes;
    wiredNodes.Create (1);

    NodeContainer rwiredNodes;
    rwiredNodes.Create(1);

    PointToPointHelper leftHelper;
    leftHelper.SetDeviceAttribute    ("DataRate", StringValue ("50Mbps"));
    leftHelper.SetChannelAttribute   ("Delay", StringValue ("2ms"));

    PointToPointHelper rightHelper;
    rightHelper.SetDeviceAttribute    ("DataRate", StringValue ("50Mbps"));
    rightHelper.SetChannelAttribute   ("Delay", StringValue ("2ms"));

    NetDeviceContainer leftNetDevices = leftHelper.Install (wiredNodes.Get (0), wirelessNodes.Get (0));
    NetDeviceContainer rightNetDevices= rightHelper.Install(wirelessNodes.Get (1),rwiredNodes.Get (0));
    


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

  ipv4.SetBase ("10.0.0.0", "255.255.255.0");

  Ipv4InterfaceContainer middleInterfaces = ipv4.Assign (wifiNetDevices);
  ipv4.NewNetwork ();
  Ipv4InterfaceContainer leftInterfaces = ipv4.Assign (leftNetDevices);

  ipv4.NewNetwork ();
  Ipv4InterfaceContainer rightInterfaces = ipv4.Assign (rightNetDevices);


  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (rightInterfaces.GetAddress (1), port));
  source.SetAttribute ("MaxBytes", UintegerValue (0));


  ApplicationContainer sourceApp = source.Install (wiredNodes.Get (0));
  sourceApp.Start (Seconds (0.0));
  sourceApp.Stop (stopTime);

PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sink.Install (rwiredNodes.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (stopTime);


   dir = "bbrgoodput-results/" + currentTime + "/";
  std::string dirToSave = "mkdir -p " + dir;
  if (system (dirToSave.c_str ()) == -1)
    {
      exit (1);
    }

  tch.Uninstall (wirelessNodes.Get (0)->GetDevice (1));
  QueueDiscContainer qd;
  qd = tch.Install (wirelessNodes.Get (0)->GetDevice (1));
  Simulator::ScheduleNow (&CheckQueueSize, qd.Get (0));

  AnimationInterface anim("trial.xml");
  anim.SetConstantPosition(wiredNodes.Get(0),10.0,10.0);
  anim.SetConstantPosition(wirelessNodes.Get(0),20.0,20.0);
  anim.SetConstantPosition(wirelessNodes.Get(1),30.0,30.0);
  anim.SetConstantPosition(rwiredNodes.Get(0),40.0,40.0);


 if (enablePcap)
    {
        phy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
      if (system ((dirToSave + "/pcap/").c_str ()) == -1)
        {
          exit (1);
        }
      phy.EnablePcapAll (dir + "/pcap/bbr", true);
    }

FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Schedule (Seconds (0 + 0.000001), &TraceGoodput, monitor);

  Simulator::Stop (stopTime + TimeStep (1));

    Simulator::Run ();
  Simulator::Destroy ();
  Time curTime = Now ();
  double averageGood = double(8 * (prev)) / double(1000 * 1000 * (100));
  std::cout << "\nAverage Goodput: " << averageGood << " Mbit/s" <<  " Total Bytes transfered"<<std::endl<<double(prev)/double(1000*1000);
  return 0;


}

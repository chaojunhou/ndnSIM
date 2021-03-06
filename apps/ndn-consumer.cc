/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Ilya Moiseenko <iliamo@cs.ucla.edu>
 */

#include "ndn-consumer.h"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"

#include "ns3/ndn-app-face.h"
#include "ns3/ndn-interest.h"
#include "ns3/ndn-content-object.h"
#include "ns3/ndnSIM/utils/ndn-fw-hop-count-tag.h"
#include "ns3/ndnSIM/utils/ndn-rtt-mean-deviation.h"

#include <boost/ref.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>

#include "ns3/names.h"

namespace ll = boost::lambda;

NS_LOG_COMPONENT_DEFINE ("ndn.Consumer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED (Consumer);

TypeId
Consumer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ndn::Consumer")
    .SetGroupName ("Ndn")
    .SetParent<App> ()
    .AddAttribute ("StartSeq", "Initial sequence number",
                   IntegerValue (0),
                   MakeIntegerAccessor(&Consumer::m_seq),
                   MakeIntegerChecker<int32_t>())

    .AddAttribute ("RandComponentLenMax", "Maximum length of randomly added component",
                   IntegerValue (0),
                   MakeIntegerAccessor(&Consumer::m_randCompLenMax),
                   MakeIntegerChecker<int32_t>())

    .AddAttribute ("Prefix","Name of the Interest",
                   StringValue ("/"),
                   MakeNameAccessor (&Consumer::m_interestName),
                   MakeNameChecker ())
    .AddAttribute ("LifeTime", "LifeTime for interest packet",
                   StringValue ("2s"),
                   MakeTimeAccessor (&Consumer::m_interestLifeTime),
                   MakeTimeChecker ())

    .AddAttribute ("RetxTimer",
                   "Timeout defining how frequent retransmission timeouts should be checked",
                   StringValue ("50ms"),
                   MakeTimeAccessor (&Consumer::GetRetxTimer, &Consumer::SetRetxTimer),
                   MakeTimeChecker ())

    .AddAttribute ("RequestMode",
                   "Determine in what sequence the consumer issues interests",
                   EnumValue (SEQUENTIAL),
                   MakeEnumAccessor (&Consumer::SetRequestMode),
                   MakeEnumChecker (SEQUENTIAL, "SEQUENTIAL",
                                    ZIPF_MANDELBROT, "ZIPF_MANDELBROT"))

    .AddAttribute ("NumberOfContents", "Total number of contents (Zipf-Mandelbrot only)",
                   StringValue ("1000"),
                   MakeUintegerAccessor (&Consumer::SetNumberOfContents, &Consumer::GetNumberOfContents),
                   MakeUintegerChecker<uint32_t> ())

    .AddAttribute ("Q", "Parameter of improve rank (Zipf-Mandelbrot only)",
                   StringValue ("0.0"),
                   MakeDoubleAccessor (&Consumer::SetQ, &Consumer::GetQ),
                   MakeDoubleChecker<double> ())

    .AddAttribute ("S", "Parameter of power (Zipf-Mandelbrot only)",
                   StringValue ("0.75"),
                   MakeDoubleAccessor (&Consumer::SetS, &Consumer::GetS),
                   MakeDoubleChecker<double> ())

    .AddTraceSource ("LastRetransmittedInterestDataDelay", "Delay between last retransmitted Interest and received Data",
                     MakeTraceSourceAccessor (&Consumer::m_lastRetransmittedInterestDataDelay))

    .AddTraceSource ("FirstInterestDataDelay", "Delay between first transmitted Interest and received Data",
                     MakeTraceSourceAccessor (&Consumer::m_firstInterestDataDelay))
    ;

  return tid;
}

Consumer::Consumer ()
  : m_rand (0, std::numeric_limits<uint32_t>::max ())
  , m_seq (0)
  , m_seqMax (0) // don't request anything
  , m_N (1000) // needed here to make sure when SetQ/SetS are called, there is a valid value of N
  , m_q (0.0)
  , m_s (0.75)
  , m_randCompLenMax (0) // no random components to be added
  , m_randCompName () // No random components
{
  NS_LOG_FUNCTION_NOARGS ();

  m_rtt = CreateObject<RttMeanDeviation> ();
}

void
Consumer::SetRequestMode (Consumer::RequestMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_requestMode = mode;
}

Consumer::RequestMode
Consumer::GetRequestMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_requestMode;
}

void
Consumer::SetNumberOfContents (uint32_t numOfContents)
{
  if (m_requestMode != ZIPF_MANDELBROT)
    return;

  m_N = numOfContents;

  NS_LOG_DEBUG (m_q << " and " << m_s << " and " << m_N);

  m_Pcum = std::vector<double> (m_N + 1);

  m_Pcum[0] = 0.0;
  for (uint32_t i=1; i<=m_N; i++)
    {
      m_Pcum[i] = m_Pcum[i-1] + 1.0 / std::pow(i+m_q, m_s);
    }

  for (uint32_t i=1; i<=m_N; i++)
    {
      m_Pcum[i] = m_Pcum[i] / m_Pcum[m_N];
      NS_LOG_LOGIC ("Cumulative probability [" << i << "]=" << m_Pcum[i]);
  }
}

uint32_t
Consumer::GetNumberOfContents () const
{
  return m_N;
}

void
Consumer::SetQ (double q)
{
  if (m_requestMode != ZIPF_MANDELBROT)
    return;

  m_q = q;
  SetNumberOfContents (m_N);
}

double
Consumer::GetQ () const
{
  return m_q;
}

void
Consumer::SetS (double s)
{
  if (m_requestMode != ZIPF_MANDELBROT)
    return;

  m_s = s;
  SetNumberOfContents (m_N);
}

double
Consumer::GetS () const
{
  return m_s;
}

void
Consumer::SetRetxTimer (Time retxTimer)
{
  m_retxTimer = retxTimer;
  if (m_retxEvent.IsRunning ())
    {
      // m_retxEvent.Cancel (); // cancel any scheduled cleanup events
      Simulator::Remove (m_retxEvent); // slower, but better for memory
    }

  // schedule even with new timeout
  m_retxEvent = Simulator::Schedule (m_retxTimer,
                                     &Consumer::CheckRetxTimeout, this);
}

Time
Consumer::GetRetxTimer () const
{
  return m_retxTimer;
}

void
Consumer::CheckRetxTimeout ()
{
  Time now = Simulator::Now ();

  Time rto = m_rtt->RetransmitTimeout ();
  // NS_LOG_DEBUG ("Current RTO: " << rto.ToDouble (Time::S) << "s");

  while (!m_seqTimeouts.empty ())
    {
      SeqTimeoutsContainer::index<i_timestamp>::type::iterator entry =
        m_seqTimeouts.get<i_timestamp> ().begin ();
      if (entry->time + rto <= now) // timeout expired?
        {
          uint32_t seqNo = entry->seq;
          m_seqTimeouts.get<i_timestamp> ().erase (entry);
          OnTimeout (seqNo);
        }
      else
        break; // nothing else to do. All later packets need not be retransmitted
    }

  m_retxEvent = Simulator::Schedule (m_retxTimer,
                                     &Consumer::CheckRetxTimeout, this);
}

// Application Methods
void
Consumer::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION_NOARGS ();

  // do base stuff
  App::StartApplication ();

  ScheduleNextPacket ();
}

void
Consumer::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION_NOARGS ();

  // cancel periodic packet generation
  Simulator::Cancel (m_sendEvent);

  // cleanup base stuff
  App::StopApplication ();
}

void
Consumer::SendPacket ()
{
  if (!m_active) return;

  NS_LOG_FUNCTION_NOARGS ();

  uint32_t seq=std::numeric_limits<uint32_t>::max (); //invalid

  while (m_retxSeqs.size ())
    {
      seq = *m_retxSeqs.begin ();
      m_retxSeqs.erase (m_retxSeqs.begin ());
      break;
    }

  if (seq == std::numeric_limits<uint32_t>::max ())
    {
      if (m_seqMax != std::numeric_limits<uint32_t>::max ())
        {
          if (m_seq >= m_seqMax)
            {
              return; // we are totally done
            }
        }

      if (m_requestMode == SEQUENTIAL)
        {
          seq = m_seq;
        }
      else
        {
          NS_ASSERT_MSG (m_seqTimeouts.size () < GetNumberOfContents (), "Content catelog exhausted!!!");
          while (m_seqTimeouts.count (seq = GetNextSeq ())); // do not send duplicate interest
        }

      m_seq ++;
    }

  Ptr<Name> nameWithSequence = Create<Name> (m_interestName);

  if (m_randCompLenMax) {
    // Do we have enough characters in the template?
    if (m_randCompLenMax >= m_randCompName.length()) {
      // No; re-create the random component name
      m_randCompName.clear();
      for (unsigned i = 0; i <= m_randCompLenMax; i++)
        m_randCompName += ('a' + (m_rand.GetInteger(0, 25)));
    }

    // Grab the random substring prefix
    nameWithSequence->Add(m_randCompName.substr(0, m_rand.GetInteger(1, m_randCompLenMax)));
  }

  (*nameWithSequence) (seq);

  Interest interestHeader;
  interestHeader.SetNonce               (m_rand.GetValue ());
  interestHeader.SetName                (nameWithSequence);
  interestHeader.SetInterestLifetime    (m_interestLifeTime);

  // NS_LOG_INFO ("Requesting Interest: \n" << interestHeader);
  NS_LOG_INFO ("> Interest for " << seq);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (interestHeader);
  NS_LOG_DEBUG ("Interest packet size: " << packet->GetSize ());
  NS_LOG_DEBUG ("Interest packet name: " << interestHeader);

  WillSendOutInterest (seq);  

  FwHopCountTag hopCountTag;
  packet->AddPacketTag (hopCountTag);

  m_transmittedInterests (&interestHeader, this, m_face);
  m_protocolHandler (packet);

  ScheduleNextPacket ();
}

uint32_t
Consumer::GetNextSeq()
{
  uint32_t content_index = 1; //[1, m_N]
  double p_sum = 0;

  double p_random = m_rand.GetValue(0.0, 1.0);
  while (p_random == 0)
    {
      p_random = m_rand.GetValue(0.0, 1.0);
    }

  for (uint32_t i=1; i<=m_N; i++)
    {
      p_sum = m_Pcum[i];   //m_Pcum[i] = m_Pcum[i-1] + p[i], p[0] = 0;   e.g.: p_cum[1] = p[1], p_cum[2] = p[1] + p[2]
      if (p_random <= p_sum)
        {
          content_index = i;
          break;
        } //if
    } //for

  return content_index;
}

///////////////////////////////////////////////////
//          Process incoming packets             //
///////////////////////////////////////////////////


void
Consumer::OnContentObject (const Ptr<const ContentObject> &contentObject,
                               Ptr<Packet> payload)
{
  if (!m_active) return;

  App::OnContentObject (contentObject, payload); // tracing inside

  NS_LOG_FUNCTION (this << contentObject << payload);

  // NS_LOG_INFO ("Received content object: " << boost::cref(*contentObject));

  uint32_t seq = boost::lexical_cast<uint32_t> (contentObject->GetName ().GetComponents ().back ());
  NS_LOG_INFO ("< DATA for " << seq << " is " << payload->GetSize() << " bytes");

  int hopCount = -1;
  FwHopCountTag hopCountTag;
  if (payload->RemovePacketTag (hopCountTag))
    {
      hopCount = hopCountTag.Get ();
    }

  SeqTimeoutsContainer::iterator entry = m_seqLastDelay.find (seq);
  if (entry != m_seqLastDelay.end ())
    {
      m_lastRetransmittedInterestDataDelay (this, seq, Simulator::Now () - entry->time, hopCount);
    }

  entry = m_seqFullDelay.find (seq);
  if (entry != m_seqFullDelay.end ())
    {
      m_firstInterestDataDelay (this, seq, Simulator::Now () - entry->time, m_seqRetxCounts[seq], hopCount);
    }

  m_seqRetxCounts.erase (seq);
  m_seqFullDelay.erase (seq);
  m_seqLastDelay.erase (seq);

  m_seqTimeouts.erase (seq);
  m_retxSeqs.erase (seq);

  m_rtt->AckSeq (SequenceNumber32 (seq));
}

void
Consumer::OnNack (const Ptr<const Interest> &interest, Ptr<Packet> origPacket)
{
  if (!m_active) return;

  App::OnNack (interest, origPacket); // tracing inside

  // NS_LOG_DEBUG ("Nack type: " << interest->GetNack ());

  // NS_LOG_FUNCTION (interest->GetName ());

  // NS_LOG_INFO ("Received NACK: " << boost::cref(*interest));
  uint32_t seq = boost::lexical_cast<uint32_t> (interest->GetName ().GetComponents ().back ());
  NS_LOG_INFO ("< NACK for " << seq);
  // std::cout << Simulator::Now ().ToDouble (Time::S) << "s -> " << "NACK for " << seq << "\n";

  // put in the queue of interests to be retransmitted
  // NS_LOG_INFO ("Before: " << m_retxSeqs.size ());
  m_retxSeqs.insert (seq);
  // NS_LOG_INFO ("After: " << m_retxSeqs.size ());

  m_seqTimeouts.erase (seq);

//  m_rtt->IncreaseMultiplier ();             // Double the next RTO ??
  ScheduleNextPacket ();
}

void
Consumer::OnTimeout (uint32_t sequenceNumber)
{
  NS_LOG_FUNCTION (sequenceNumber);
  // std::cout << Simulator::Now () << ", TO: " << sequenceNumber << ", current RTO: " << m_rtt->RetransmitTimeout ().ToDouble (Time::S) << "s\n";

//  m_rtt->IncreaseMultiplier ();             // Double the next RTO
  m_rtt->SentSeq (SequenceNumber32 (sequenceNumber), 1); // make sure to disable RTT calculation for this sample
  m_retxSeqs.insert (sequenceNumber);
  ScheduleNextPacket ();
}

void
Consumer::WillSendOutInterest (uint32_t sequenceNumber)
{
  NS_LOG_DEBUG ("Trying to add " << sequenceNumber << " with " << Simulator::Now () << ". already " << m_seqTimeouts.size () << " items");

  m_seqTimeouts.insert (SeqTimeout (sequenceNumber, Simulator::Now ()));
  m_seqFullDelay.insert (SeqTimeout (sequenceNumber, Simulator::Now ()));

  m_seqLastDelay.erase (sequenceNumber);
  m_seqLastDelay.insert (SeqTimeout (sequenceNumber, Simulator::Now ()));

  m_seqRetxCounts[sequenceNumber] ++;

  m_rtt->SentSeq (SequenceNumber32 (sequenceNumber), 1);
}

} // namespace ndn
} // namespace ns3

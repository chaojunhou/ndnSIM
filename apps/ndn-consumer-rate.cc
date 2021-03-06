/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ndn-consumer-rate.h"
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

#include <boost/lexical_cast.hpp>

NS_LOG_COMPONENT_DEFINE ("ndn.ConsumerRate");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED (ConsumerRate);

TypeId
ConsumerRate::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ndn::ConsumerRate")
    .SetGroupName ("Ndn")
    .SetParent<Consumer> ()
    .AddConstructor<ConsumerRate> ()

    .AddAttribute ("MaxSeq",
                   "Maximum sequence number to request",
                   IntegerValue (std::numeric_limits<uint32_t>::max ()),
                   MakeIntegerAccessor (&ConsumerRate::m_seqMax),
                   MakeIntegerChecker<uint32_t> ())
    .AddAttribute ("Frequency",
                   "Initial interest packet sending frequency in hertz",
                   DoubleValue (10.0),
                   MakeDoubleAccessor (&ConsumerRate::m_frequency),
                   MakeDoubleChecker<double> ())
    ;

  return tid;
}

ConsumerRate::ConsumerRate ()
  : m_firstTime (true)
{
}

ConsumerRate::~ConsumerRate ()
{
}

void
ConsumerRate::ScheduleNextPacket ()
{
  if (m_firstTime)
    {
      m_sendEvent = Simulator::Schedule (Seconds (0.0),
                                         &Consumer::SendPacket, this);
      m_firstTime = false;
    }
  else if (!m_sendEvent.IsRunning ())
    m_sendEvent = Simulator::Schedule (Seconds(1.0 / m_frequency),
                                       &Consumer::SendPacket, this);
}

void
ConsumerRate::OnContentObject (const Ptr<const ContentObject> &contentObject,
                               Ptr< Packet> payload)
{
  AdjustFrequencyOnContentObject (contentObject, payload);
  Consumer::OnContentObject (contentObject, payload);
}

void
ConsumerRate::OnNack (const Ptr<const Interest> &interest, Ptr<Packet> payload)
{
  AdjustFrequencyOnNack (interest, payload);
  Consumer::OnNack (interest, payload);
}

void
ConsumerRate::OnTimeout (uint32_t sequenceNumber)
{
  AdjustFrequencyOnTimeout (sequenceNumber);
  Consumer::OnTimeout (sequenceNumber);
}

void
ConsumerRate::AdjustFrequencyOnContentObject (const Ptr<const ContentObject> &contentObject,
                                                Ptr<Packet> payload)
{
  NS_LOG_DEBUG ("Current frequency: " << m_frequency);
}

void
ConsumerRate::AdjustFrequencyOnNack (const Ptr<const Interest> &interest,
                                       Ptr<Packet> payload)
{
  NS_LOG_DEBUG ("Current frequency: " << m_frequency);
}

void
ConsumerRate::AdjustFrequencyOnTimeout (uint32_t sequenceNumber)
{
  NS_LOG_DEBUG ("Current frequency: " << m_frequency);
}

} // namespace ndn
} // namespace ns3

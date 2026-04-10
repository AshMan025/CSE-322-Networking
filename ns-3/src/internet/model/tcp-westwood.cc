/*
 * TCP Westwood Implementation for NS-3
 */

#include "tcp-westwood.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/traced-value.h"
#include "ns3/uinteger.h"
#include "tcp-socket-base.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TcpWestwood");

NS_OBJECT_ENSURE_REGISTERED(TcpWestwood);

TypeId TcpWestwood::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::TcpWestwood")
          .SetParent<TcpNewReno>()
          .SetGroupName("Internet")
          .AddConstructor<TcpWestwood>()
          .AddAttribute("Tau", "The filter time constant (default 500ms)",
                        TimeValue(MilliSeconds(500)),
                        MakeTimeAccessor(&TcpWestwood::m_baseTau),
                        MakeTimeChecker())
          .AddAttribute("MaxTau", "Maximum limit for adaptive tau",
                        TimeValue(MilliSeconds(2000)),
                        MakeTimeAccessor(&TcpWestwood::m_maxTau),
                        MakeTimeChecker())
          .AddAttribute("MinTau", "Minimum limit for adaptive tau",
                        TimeValue(MilliSeconds(50)),
                        MakeTimeAccessor(&TcpWestwood::m_minTau),
                        MakeTimeChecker())
          .AddAttribute("RecentRttWindowSize",
                        "Number of recent RTT samples used by the adaptive tau heuristic",
                        UintegerValue(4),
                        MakeUintegerAccessor(&TcpWestwood::m_recentRttWindowSize),
                        MakeUintegerChecker<uint32_t>(2, 16))
          .AddTraceSource("CurrentTau", "Current value of Tau",
                          MakeTraceSourceAccessor(&TcpWestwood::m_tau),
                          "ns3::Time::TracedValueCallback");
  return tid;
}



TcpWestwood::TcpWestwood()
    : TcpNewReno(), m_currentBwe(0.0), m_filteredBwe(0.0),
      m_lastAckTime(Seconds(0)), m_minRtt(Time::Max()), m_ackedCount(0),
      m_accountedFor(0), m_rttVariance(0.0), m_avgRtt(Seconds(0)),
      m_recentAvgRtt(Seconds(0)), m_recentRttWindowSize(4), m_lossCount(0),
      m_recentRttSum(0.0) {
  NS_LOG_FUNCTION(this);
  m_tau = m_baseTau; // Initialize tau
}



TcpWestwood::TcpWestwood(const TcpWestwood &sock)
    : TcpNewReno(sock), m_currentBwe(sock.m_currentBwe),
      m_filteredBwe(sock.m_filteredBwe), m_lastAckTime(sock.m_lastAckTime),
      m_minRtt(sock.m_minRtt), m_ackedCount(sock.m_ackedCount),
      m_accountedFor(sock.m_accountedFor), m_tau(sock.m_tau),
      m_baseTau(sock.m_baseTau), m_rttVariance(sock.m_rttVariance),
      m_avgRtt(sock.m_avgRtt), m_recentAvgRtt(sock.m_recentAvgRtt),
      m_minTau(sock.m_minTau), m_maxTau(sock.m_maxTau),
      m_recentRttWindowSize(sock.m_recentRttWindowSize),
      m_lossCount(sock.m_lossCount), m_recentRttSamples(sock.m_recentRttSamples),
      m_recentRttSum(sock.m_recentRttSum) {
  NS_LOG_FUNCTION(this);
}

TcpWestwood::~TcpWestwood() { NS_LOG_FUNCTION(this); }

std::string TcpWestwood::GetName() const { return "TcpWestwood"; }

Ptr<TcpCongestionOps> TcpWestwood::Fork() {
  return CopyObject<TcpWestwood>(this);
}

uint32_t TcpWestwood::AckedCount(uint32_t segmentsAcked,
                                 Ptr<TcpSocketState> tcb) {
 

  // In simple terms for NS-3's API:
  uint32_t bytesAcked = segmentsAcked * tcb->m_segmentSize;
  m_ackedCount += bytesAcked;
  return bytesAcked;
}


// ok
void TcpWestwood::AdaptTau(const Time &currentRtt) {
  if (currentRtt.IsZero()) {
    return;
  }

  const double alpha = 0.125;
  const double currentRttS = currentRtt.ToDouble(Time::S);

  if (m_avgRtt.IsZero()) {
    m_avgRtt = currentRtt;
  } else {
    m_avgRtt = Time::FromDouble((1 - alpha) * m_avgRtt.ToDouble(Time::S) +
                                    alpha * currentRttS,
                                Time::S);
  }

  m_recentRttSamples.push_back(currentRttS);
  m_recentRttSum += currentRttS;
  while (m_recentRttSamples.size() > m_recentRttWindowSize) {
    m_recentRttSum -= m_recentRttSamples.front();
    m_recentRttSamples.pop_front();
  }

  const double recentAvgS =
      m_recentRttSum / static_cast<double>(m_recentRttSamples.size());
  const double longAvgS = std::max(m_avgRtt.ToDouble(Time::S), 1e-6);
  m_recentAvgRtt = Time::FromDouble(recentAvgS, Time::S);

  double recentVariance = 0.0;
  for (double sample : m_recentRttSamples) {
    const double diff = sample - recentAvgS;
    recentVariance += diff * diff;
  }
  recentVariance /= static_cast<double>(m_recentRttSamples.size());
  m_rttVariance = recentVariance;

  const double recentJitterRatio = std::sqrt(m_rttVariance) / longAvgS;
  const double recentShiftRatio = std::abs(recentAvgS - longAvgS) / longAvgS;
  const double stressScore = 0.65 * recentShiftRatio + 0.35 * recentJitterRatio;

  const double fillRatio =
      std::min(1.0, static_cast<double>(m_recentRttSamples.size()) /
                        static_cast<double>(m_recentRttWindowSize));
  const double normalizedStress = std::min(1.0, stressScore / 0.25);
  double tauMultiplier = 0.60 + normalizedStress * 1.40;
  tauMultiplier = 1.0 + (tauMultiplier - 1.0) * fillRatio;

  Time newTau =
      Time::FromDouble(m_baseTau.ToDouble(Time::S) * tauMultiplier, Time::S);

  if (newTau > m_maxTau) {
    newTau = m_maxTau;
  }
  if (newTau < m_minTau) {
    newTau = m_minTau;
  }

  m_tau = newTau;

  NS_LOG_INFO(Simulator::Now().GetSeconds()
              << " RTTcur=" << currentRtt.GetMilliSeconds()
              << "ms RTTrecent=" << m_recentAvgRtt.GetMilliSeconds()
              << "ms RTTlong=" << m_avgRtt.GetMilliSeconds()
              << "ms Var=" << m_rttVariance
              << " Stress=" << stressScore
              << " Tau=" << m_tau.Get().GetMilliSeconds() << "ms");
}

void TcpWestwood::FilterBW(Time currentTime) {
  // Tustin Filter (Equation 1)
  // b_hat_k = alpha * b_hat_k_1 + (1-alpha)/2 * (b_k + b_k_1)
  // But coefficient depends on time.
  // coeff = (tau - delta) / (tau + delta) ... wait, check paper/task equation.

  // Task says:
  // bk_hat = [(tau - delta) * bk-1_hat + (delta/2) * (bk + bk-1)] / (tau +
  // delta) where delta = tk - tk-1

  if (m_lastAckTime.IsZero()) {
    // First sample
    m_filteredBwe = m_currentBwe;
    return;
  }

  double delta = (currentTime - m_lastAckTime).ToDouble(Time::S);
  double tau = m_tau.Get().ToDouble(Time::S);

  if (delta <= 0)
    return; // Should not happen if time advances

  if (m_filteredBwe == 0.0) {
    m_filteredBwe = m_currentBwe;
  }

  double sample = m_currentBwe;

  // Correct Tustin bilinear transform for a 1st order low-pass filter:
  

  double num = (2 * tau - delta) * m_filteredBwe + 2 * delta * sample;
  double den = 2 * tau + delta;

  if (den > 0) {
    m_filteredBwe = num / den;
  }

  NS_LOG_INFO("FilterBW: delta=" << delta << "s sample=" << sample / 1e6
                                 << "Mbps filtered=" << m_filteredBwe / 1e6
                                 << "Mbps");
}

void TcpWestwood::EstimateBW(uint32_t segmentsAcked, const Time &rtt,
                             Ptr<TcpSocketState> tcb) {
  Time now = Simulator::Now();

  uint32_t bytes = segmentsAcked * tcb->m_segmentSize;
  m_accountedFor += bytes;


  // On the very first ACK used for BWE estimation
  if (m_lastAckTime.IsZero()) {
    m_lastAckTime = now;
    return;
  }

  // get the difernece in time 
  Time delta = now - m_lastAckTime;
  double deltaS = delta.ToDouble(Time::S);

  // To prevent wild fluctuations from ACK compression and delayed ACKs,
  // we accumulate acked bytes and only compute a raw BWE sample if
  // at least an RTT has passed. This aligns with Westwood+ robust sampling.
  if (delta < rtt && !rtt.IsZero()) {
    return;
  }

  // Ensure delta is not dangerously small in extreme cases
  if (deltaS < 0.001) {
    return;
  }

  m_currentBwe = (m_accountedFor * 8.0) / deltaS; // bits per second
  m_accountedFor = 0; // reset for next tracking interval********

  // Run Adaptive Tau
  AdaptTau(rtt);  // variance calculations using the rtt and based on this we calculate the cwnd

  // Run Filter
  FilterBW(now);

  // Update state
  m_lastAckTime = now;

  // Min RTT tracking
  if (rtt < m_minRtt && !rtt.IsZero()) {
    m_minRtt = rtt;
  }
}

void TcpWestwood::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                            const Time &rtt) {
  NS_LOG_FUNCTION(this << segmentsAcked << rtt);

  if (segmentsAcked > 0) {
    EstimateBW(segmentsAcked, rtt, tcb);
  }

  // Call parent, westwood uses Renos increase logic....
  TcpNewReno::PktsAcked(tcb, segmentsAcked, rtt);
}

void TcpWestwood::IncreaseWindow(Ptr<TcpSocketState> tcb,
                                 uint32_t segmentsAcked) {
  // Standard NewReno behavior for increase
  // Because TcpWestwood is choosing not to change the window-growth rule.
  TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
}

uint32_t TcpWestwood::GetSsThresh(Ptr<const TcpSocketState> tcb,
                                  uint32_t bytesInFlight) {
  // Westwood ssthresh = (BWE * RTTmin) / seg_size
  // If undefined, fallback to standard

  if (m_filteredBwe > 0 && m_minRtt < Time::Max()) {
    double bweParams = m_filteredBwe * m_minRtt.ToDouble(Time::S); // bits
    uint32_t ssthresh =
        static_cast<uint32_t>(bweParams / 8.0 / tcb->m_segmentSize); // segments

    // Ensure at least 2 segments
    if (ssthresh < 2)
      ssthresh = 2;

    NS_LOG_INFO("GetSsThresh: BW=" << m_filteredBwe / 1e6
                                   << "Mbps RTT=" << m_minRtt.GetMilliSeconds()
                                   << "ms -> ssthresh=" << ssthresh);

    return ssthresh * tcb->m_segmentSize; // Return bytes
  }

  return TcpNewReno::GetSsThresh(tcb, bytesInFlight);
}

} // namespace ns3

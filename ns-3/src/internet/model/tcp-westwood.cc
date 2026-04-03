/*
 * TCP Westwood Implementation for NS-3
 */

#include "tcp-westwood.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/traced-value.h"
#include "tcp-socket-base.h"

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
          .AddTraceSource("CurrentTau", "Current value of Tau",
                          MakeTraceSourceAccessor(&TcpWestwood::m_tau),
                          "ns3::Time::TracedValueCallback");
  return tid;
}



TcpWestwood::TcpWestwood()
    : TcpNewReno(), m_currentBwe(0.0), m_filteredBwe(0.0),
      m_lastAckTime(Seconds(0)), m_minRtt(Time::Max()), m_ackedCount(0),
      m_accountedFor(0), m_rttVariance(0.0), m_avgRtt(Seconds(0)),
      m_lossCount(0) {
  NS_LOG_FUNCTION(this);
  m_tau = m_baseTau; // Initialize tau
}



TcpWestwood::TcpWestwood(const TcpWestwood &sock)
    : TcpNewReno(sock), m_currentBwe(sock.m_currentBwe),
      m_filteredBwe(sock.m_filteredBwe), m_lastAckTime(sock.m_lastAckTime),
      m_minRtt(sock.m_minRtt), m_ackedCount(sock.m_ackedCount),
      m_accountedFor(sock.m_accountedFor), m_tau(sock.m_tau),
      m_baseTau(sock.m_baseTau), m_rttVariance(sock.m_rttVariance),
      m_avgRtt(sock.m_avgRtt), m_minTau(sock.m_minTau), m_maxTau(sock.m_maxTau),
      m_lossCount(sock.m_lossCount) {
  NS_LOG_FUNCTION(this);
}

TcpWestwood::~TcpWestwood() { NS_LOG_FUNCTION(this); }

std::string TcpWestwood::GetName() const { return "TcpWestwood"; }

Ptr<TcpCongestionOps> TcpWestwood::Fork() {
  return CopyObject<TcpWestwood>(this);
}

uint32_t TcpWestwood::AckedCount(uint32_t segmentsAcked,
                                 Ptr<TcpSocketState> tcb) {
  // Basic implementation of AckedCount based on cumulative ACKs
  // Section 2.3 of the paper discusses handling delayed ACKs.
  // In NS-3, segmentsAcked tells us how many full segments were acked.
  // We can use that directly or implement the more complex logic if we were
  // parsing raw ACKs. Given TcpSocketState abstraction, we use segmentsAcked *
  // segmentSize.

  // NOTE: paper logic for d_k:
  // if (ack > accounted_for) {
  //    d_k = ack - accounted_for;
  //    accounted_for = ack;
  // }

  // In simple terms for NS-3's API:
  uint32_t bytesAcked = segmentsAcked * tcb->m_segmentSize;
  m_ackedCount += bytesAcked;
  return bytesAcked;
}

void TcpWestwood::AdaptTau(const Time &currentRtt) {
  // Adaptive Tau Algorithm
  // 1. Calculate RTT variance (EWMA)
  // 2. Adjust Tau based on variance

  double alpha = 0.125;

  if (m_avgRtt.IsZero()) {
    m_avgRtt = currentRtt;
  } else {
    m_avgRtt = Time::FromDouble((1 - alpha) * m_avgRtt.ToDouble(Time::S) +
                                    alpha * currentRtt.ToDouble(Time::S),
                                Time::S);

    double diff =
        std::abs(currentRtt.ToDouble(Time::S) - m_avgRtt.ToDouble(Time::S));
    m_rttVariance = (1 - alpha) * m_rttVariance + alpha * (diff * diff);
  }

  // Thresholds for variance (heuristic)
  // High variance indicates instability -> increase Tau for stability
  // Low variance -> decrease Tau for agility

  // These thresholds might need tuning.
  // Let's say if variance > (20ms)^2 we consider it high.
  double highThreshold = 0.0004;  // 0.02 * 0.02
  double lowThreshold = 0.000025; // 0.005 * 0.005

  Time newTau = m_baseTau;

  if (m_rttVariance > highThreshold) {
    newTau = Time::FromDouble(m_baseTau.ToDouble(Time::S) * 2.0, Time::S);
  } else if (m_rttVariance < lowThreshold) {
    // Only if stable, maybe perform heuristic on loss?
    // For now, simple variance based adaptation.
    newTau = Time::FromDouble(m_baseTau.ToDouble(Time::S) * 0.5, Time::S);
  }

  // Clamp values
  if (newTau > m_maxTau)
    newTau = m_maxTau;
  if (newTau < m_minTau)
    newTau = m_minTau;

  m_tau = newTau;

  NS_LOG_INFO(Simulator::Now().GetSeconds()
              << " RTT=" << m_avgRtt.GetMilliSeconds()
              << " Var=" << m_rttVariance
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

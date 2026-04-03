/*
 * TCP Westwood Implementation for NS-3
 * Based on the original TCP Westwood paper and requirements.
 */

#ifndef TCP_WESTWOOD_ALGORITHM_H
#define TCP_WESTWOOD_ALGORITHM_H

#include "ns3/traced-value.h"
#include "tcp-congestion-ops.h"
// #include "ns3/time.h" // Removed to avoid include error
namespace ns3 {

class Time;
class TcpSocketState;

/**
 * @brief Implementation of TCP Westwood Congestion Control
 *
 * This implementation follows the original TCP Westwood algorithm:
 * - Bandwidth estimation via ACK monitoring (bk = dk / delta_t)
 * - Tustin filtering for bandwidth estimate (Equation 1)
 * - Adaptive ssthresh setting after loss
 *
 * It also includes an "Adaptive Tau" enhancement to dynamically adjust
 * the filter time constant based on network conditions.
 */
class TcpWestwood : public TcpNewReno {
public:
  /**
   * @brief Get the type ID.
   * @return the object TypeId
   */
  static TypeId GetTypeId(void);

  TcpWestwood();

  /**
   * @brief Copy constructor
   */
  TcpWestwood(const TcpWestwood &sock);

  virtual ~TcpWestwood();

  std::string GetName() const override;

  // Overrides from TcpCongestionOps
  virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                         const Time &rtt) override;
  virtual void IncreaseWindow(Ptr<TcpSocketState> tcb,
                              uint32_t segmentsAcked) override;
  virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                               uint32_t bytesInFlight) override;
  virtual Ptr<TcpCongestionOps> Fork() override;

private:
  // Bandwidth Estimation State
  double m_currentBwe;  //!< Current raw bandwidth estimate (bps)
  double m_filteredBwe; //!< Filtered bandwidth estimate (bps)
  Time m_lastAckTime;   //!< Time of last ACK reception
  Time m_minRtt;        //!< Minimum RTT observed

  uint32_t
      m_ackedCount; //!< Accumulator for acked bytes (handling delayed ACKs)
  uint32_t m_accountedFor; //!< Max cumulative ACK accounted for (Section 2.3)

  // Filter Parameters
  TracedValue<Time> m_tau; //!< Current filter time constant (default 500ms)
  Time m_baseTau;          //!< Base time constant

  // Adaptive Tau State
  double m_rttVariance; //!< Estimated RTT variance
  Time m_avgRtt;        //!< Average RTT (smoothed)
  Time m_minTau;        //!< Minimum allowed tau
  Time m_maxTau;        //!< Maximum allowed tau
  uint32_t m_lossCount; //!< Loss events counter (if needed for heuristic)

  // Internal Methods
  void EstimateBW(uint32_t segmentsAcked, const Time &rtt,
                  Ptr<TcpSocketState> tcb);
  void FilterBW(Time currentTime);
  uint32_t AckedCount(uint32_t segmentsAcked, Ptr<TcpSocketState> tcb);
  void AdaptTau(const Time &currentRtt);
};

} // namespace ns3

#endif // TCP_WESTWOOD_ALGORITHM_H

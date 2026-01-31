// gs_opengolfsim_interface.h

#pragma once
#include "gs_results.h"
#include "gs_sim_socket_interface.h"
#include <string>

namespace golf_sim {

class GsOpenGolfSimInterface : public GsSimSocketInterface {
public:
  GsOpenGolfSimInterface();
  virtual ~GsOpenGolfSimInterface();

  static bool InterfaceIsPresent();

  virtual bool Initialize() override;
  virtual void DeInitialize() override;

  virtual bool SendResults(const GsResults &results) override;

  virtual void SetSimSystemArmed(const bool is_armed) override;
  virtual bool GetSimSystemArmed() override;

protected:
  std::string EnsureNewline(const std::string &s);
  virtual std::string GenerateResultsDataToSend(const GsResults &r);
  virtual bool ProcessReceivedData(const std::string received_data) override;
  void OnConnectionStateChanged(SimConnState from, SimConnState to,
                                const std::string &reason) override;

private:
  std::string last_device_status_;
};

} // namespace golf_sim

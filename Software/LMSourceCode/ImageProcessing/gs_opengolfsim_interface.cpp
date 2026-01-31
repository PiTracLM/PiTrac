#include <cstdlib>
#include <string>

#include "gs_config.h"
#include "gs_options.h"
#include "gs_ui_system.h"
#include "logging_tools.h"

#include "gs_opengolfsim_interface.h"
#include "gs_opengolfsim_results.h"

namespace golf_sim {

static const int kDefaultOGSPort = 3111;
static const char *kDefaultOGSHost = ""; // empty = disabled by default

static std::string ToString(SimConnState s) {
  switch (s) {
  case SimConnState::kDisabled:
    return "Disabled";
  case SimConnState::kDisconnected:
    return "Disconnected";
  case SimConnState::kConnecting:
    return "Connecting";
  case SimConnState::kConnected:
    return "Connected";
  case SimConnState::kError:
    return "Error";
  }
  return "Unknown";
}

void GsOpenGolfSimInterface::OnConnectionStateChanged(
    SimConnState /*from*/, SimConnState to, const std::string &reason) {
  GsIPCResultType uiType = GsIPCResultType::kWaitingForSimulatorArmed;
  std::string human;

  switch (to) {
  case SimConnState::kDisabled:
    human = "OpenGolfSim disabled (no host/port configured)";
    break;

  case SimConnState::kConnecting:
    human = "OpenGolfSim connecting to " + socket_connect_address_ + ":" +
            socket_connect_port_;
    break;

  case SimConnState::kConnected:
    break;

  case SimConnState::kDisconnected:
    human =
        "OpenGolfSim disconnected" + (reason.empty() ? "" : (": " + reason));
    break;

  case SimConnState::kError:
  default:
    uiType = GsIPCResultType::kError;
    human =
        "OpenGolfSim socket error" + (reason.empty() ? "" : (": " + reason));
    break;
  }

  // machine tag (Python parses this)
  std::string state = (to == SimConnState::kConnected)      ? "connected"
                      : (to == SimConnState::kConnecting)   ? "connecting"
                      : (to == SimConnState::kDisabled)     ? "disabled"
                      : (to == SimConnState::kDisconnected) ? "disconnected"
                                                            : "error";

  std::string tag = "SIM_CONN OpenGolfSim state=" + state;
  if (!reason.empty())
    tag += " reason=" + reason;

  // ONE message: easy parse + still readable
  std::string combined = "[" + tag + "] " + human;

  GsUISystem::SendIPCStatusMessage(uiType, combined);
}

std::string GsOpenGolfSimInterface::EnsureNewline(const std::string &s) {
  if (!s.empty() && s.back() == '\n')
    return s;
  return s + "\n";
}

GsOpenGolfSimInterface::GsOpenGolfSimInterface() {
  // Defaults
  socket_connect_address_ = kDefaultOGSHost;
  socket_connect_port_ =
      std::to_string(kDefaultOGSPort); // <-- port is a string in base class
  last_device_status_.clear();

  // Pull from config
  GolfSimConfiguration::SetConstant(
      "gs_config.golf_simulator_interfaces.OpenGolfSim.kOGSConnectAddress",
      socket_connect_address_);
  GolfSimConfiguration::SetConstant(
      "gs_config.golf_simulator_interfaces.OpenGolfSim.kOGSConnectPort",
      socket_connect_port_);
}

GsOpenGolfSimInterface::~GsOpenGolfSimInterface() {}

bool GsOpenGolfSimInterface::InterfaceIsPresent() {
  // Read config into locals (static function!)
  std::string addr = kDefaultOGSHost;
  std::string port_str = std::to_string(kDefaultOGSPort);

  GolfSimConfiguration::SetConstant(
      "gs_config.golf_simulator_interfaces.OpenGolfSim.kOGSConnectAddress",
      addr);
  GolfSimConfiguration::SetConstant(
      "gs_config.golf_simulator_interfaces.OpenGolfSim.kOGSConnectPort",
      port_str);

  int port = -1;
  try {
    port = std::stoi(port_str);
  } catch (...) {
    port = -1;
  }

  // Treat empty/"disabled" as off
  if (addr.empty() || addr == "disabled" || port <= 0) {
    GS_LOG_TRACE_MSG(
        trace, "GsOpenGolfSimInterface::InterfaceIsPresent - Not Present");
    return false;
  }

  GS_LOG_TRACE_MSG(
      trace, "GsOpenGolfSimInterface::InterfaceIsPresent - Present (addr=" +
                 addr + ", port=" + std::to_string(port) + ")");
  return true;
}

bool GsOpenGolfSimInterface::Initialize() {
  GS_LOG_TRACE_MSG(trace, "GsOpenGolfSimInterface Initialize called.");

  // Log what we will actually try to connect to
  GS_LOG_MSG(info, "OpenGolfSim connect target: " + socket_connect_address_ +
                       ":" + socket_connect_port_);

  if (!GsSimSocketInterface::Initialize()) {
    GS_LOG_MSG(error, "GsOpenGolfSimInterface could not Initialize.");
    return false;
  }

#ifdef __unix__
  usleep(500);
#endif

  initialized_ = true;

  // Always ready on connect
  SendSimMessage(EnsureNewline(R"({"type":"device","status":"ready"})"));
  last_device_status_ = "ready"; // avoid immediate duplicate heartbeat
  return true;
}

void GsOpenGolfSimInterface::DeInitialize() {
  GsSimSocketInterface::DeInitialize();
}

void GsOpenGolfSimInterface::SetSimSystemArmed(const bool /*is_armed*/) {}

bool GsOpenGolfSimInterface::GetSimSystemArmed() { return true; }

bool GsOpenGolfSimInterface::SendResults(const GsResults &r) {
  if (!initialized_)
    return false;

  // Heartbeat -> device status
  if (r.result_message_is_keepalive_) {
    const char *status = r.heartbeat_ball_detected_ ? "ready" : "busy";
    if (last_device_status_ == status)
      return true;
    last_device_status_ = status;

    std::string msg =
        std::string(R"({"type":"device","status":")") + status + R"("})";
    SendSimMessage(EnsureNewline(msg));
    return true;
  }

  // Normal shot path
  const std::string msg = GenerateResultsDataToSend(r);
  if (msg.empty())
    return true;
  SendSimMessage(msg);
  return true;
}

std::string
GsOpenGolfSimInterface::GenerateResultsDataToSend(const GsResults &r) {
  GsOpenGolfSimResults ogs(r);
  const std::string payload = ogs.Format();
  if (payload.empty())
    return "";
  return EnsureNewline(payload);
}

bool GsOpenGolfSimInterface::ProcessReceivedData(
    const std::string received_data) {
  GS_LOG_MSG(info, "Received from OpenGolfSim: " + received_data);
  return true;
}

} // namespace golf_sim

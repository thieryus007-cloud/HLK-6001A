// Custom ESPHome external component for the Hi-Link HLK-LD6001A 60GHz
// multi-target mmWave radar (MS72SF1 chipset). Architecture, web UI and HTTP
// API ported from the final state of the HLK-LD6001B project
// (HLK-LD6001B/ESP32S3_Plus, 2026-09-22 -- same XIAO ESP32-S3 Plus board,
// same external-component pattern); protocol handling specific to this
// module -- see ../../../PROTOCOL.md for the field reference and
// ../../../PLAN.md for the evidence behind every choice below.
//
// Operating mode: AT+DEBUG=2 (RADAR_OPERATING_DEBUG_MODE). The manual calls
// it the "debug mode (used by the host computer)"; on the real module
// (2026-09-25 capture, testing/fixtures/debug2_rx_2026-09-25.bin) it emits
// the same TLV frames as the documented AT+DEBUG=3, WITH the point cloud
// (POINTLEN > 0, always a multiple of 25) -- which the HLK page needs --
// and WITHOUT the trailing checksum byte AT+DEBUG=3 has. Both framings are
// accepted by the parser (decide_tlv_framing_()), so switching to DEBUG=3
// is a one-constant change.
//
// TLV frame (little-endian):
//   HEAD 8 (01..08) | LENGTH 4 | FRAME 4 | TLV1=1 4 | POINTLEN 4 |
//   <points, 25 bytes each> | TLV2=2 4 | TRACKLEN 4 |
//   <persons, 32 bytes each: Q, ID, X, Y, Z, Vx, Vy, Vz> | [CHECK 1]
//   DEBUG=2: LENGTH = exact frame size, no CHECK (349/349 real frames).
//   DEBUG=3: LENGTH excludes a trailing CHECK = XOR(FRAME + person records)
//            (manual's worked example, 0xCC).
//   Structural validation in both: TLV1 == 1, TLV2 == 2,
//   LENGTH == 24 + POINTLEN + 8 + TRACKLEN exactly.
// Reference decoder, same decision rules: testing/radar_protocol_tlv.py.
//
// Point record (25 bytes): X,Y,Z float32 @0/4/8, tag int8 @12, D float32 @13
// (E/F @17/21 not stored) -- layout HYPOTHESIS carried over from the 6001B
// (validated there against ground truth), not yet validated against ground
// truth on this module. Only X/Y/Z/D are exposed, D drives the vendor-tool
// colour legend on the HLK page.
//
// Radar firmware on the delivered module: "NOP_2.11-20260525-minesemi"
// (MinewSemi). Its AT+ command set differs from the Hi-Link manual -- every
// command below was tested on the real module 2026-09-25, each mapped to its
// AT+READ key by a change-then-restore test (PLAN.md, Journal Phase 1):
//   AT+DPKTHF (DPKF, far sensitivity -- the manual's AT+DPKTH answers AT+ERR)
//   AT+DPKTHN (DPKN, near sensitivity -- in neither manual)
//   AT+RANGE (Range), AT+HEIGHT (Height -- AT+HEIGHTD answers AT+ERR),
//   AT+HRANGE (Hrange -- in neither manual), AT+HEATIME (Heart_Time),
//   AT+XNegaD/XPosiD/YNegaD/YPosiD (Xdetection*/Ydetection* -- WITH the "D",
//   the form without it answers AT+ERR).
// AT+Moving/AT+Static/AT+Exit (manual Hi-Link) answer AT+ERR in every
// spelling tried -- not used.
//
// AT+READ: replies with a one-line pseudo-JSON block ('{ "SoftVerison":...,
// "RangeRes":..., ..., "YdetectionP":300 }'), not an AT+OK line -- captured
// by read_capture_* below and parsed key by key (parse_read_response_()),
// feeding the top-bar "Etat radar" and the per-field conformity of the
// Configure page. It answers AT+ERR while the radar streams and the block
// once stopped (both observed 2026-09-25) -- so it is sent from
// apply_radar_settings_(), after AT+STOP and before the final AT+START.
//
// The default AT+DEBUG=0 protocol (55 AA, TYPE 0x04 = person count only) is
// still recognised and checksum-validated (keeps the watchdog fed, logs the
// first frame of each TYPE for the heartbeat question -- AT+HEATIME exists,
// its frame layout is undocumented), never decoded further.
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "esphome/components/uart/uart.h"
#include "esphome/core/preferences.h"
// Setup Details health indicators (RSSI/SSID) -- same mechanism as the 6001B.
#include "esphome/components/wifi/wifi_component.h"

#include <esp_http_server.h>
#include <esp_system.h>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace esphome::hlk_ld6001a {

// Explicit user decision 2026-09-09 (PLAN.md, "Points ouverts"): 6 targets,
// matching the web UI's fixed 6-slot target table.
static const uint8_t MAX_TARGETS = 6;

// Raw point-cloud cap -- same value as the 6001B (busiest real 6001A frame so
// far: 4 points; 6001B: 15). Bounds heap/JSON size against radar noise.
static const uint16_t MAX_POINTS = 150;

// See the header comment: 2 = host-computer mode (TLV + point cloud, no
// checksum), 3 = documented detailed protocol (TLV + checksum, no points).
static const uint8_t RADAR_OPERATING_DEBUG_MODE = 2;

// Installation geometry filter -- no real target can be within 30cm of a
// ceiling-mounted radar.
static const float MIN_TARGET_DISTANCE_M = 0.30f;

// Self-healing watchdog -- same thresholds as the 6001B.
static const uint32_t WATCHDOG_TIMEOUT_MS = 90000;
static const uint32_t WATCHDOG_COOLDOWN_MS = 30000;

// AT+ command queue ack timeout -- same value as the 6001B.
static const uint32_t AT_COMMAND_ACK_TIMEOUT_MS = 5000;

// apply_radar_settings_() enqueues 12 commands (AT+STOP, 6 settings, 4 zone
// bounds, AT+READ), leaving room for console commands on top.
static const size_t MAX_AT_COMMAND_QUEUE_SIZE = 32;

// HA sensor publish rate limit (not /hlk_targets.json, which stays raw) --
// explicit user decision 2026-09-09, "to revisit during testing".
static const uint32_t PUBLISH_THROTTLE_MS = 1000;

// Debounce for the HA target count INCREASES only -- never applied to
// has_target (presence stays instant) nor to decreases, nor to
// /hlk_targets.json (raw view). Same constant and logic as the 6001B
// (TARGET_COUNT_INCREASE_DEBOUNCE_MS in hlk_ld6001b.h): a higher count is
// published only once it has held unchanged for this long. Side-by-side
// measurement 2026-09-26 (PLAN.md): the 6001A showed transient extra
// tracks (~25 s static at ~4 m, ~45 s wandering near the radar) that the
// 6001B never published; replaying this debounce on the recorded 6001A
// counts matched the 6001B's published count on 180/180 samples (104/180
// without it).
static const uint32_t TARGET_COUNT_INCREASE_DEBOUNCE_MS = 60000;

// AT+READ capture window -- the real reply is ~420 characters; generous
// headroom, bounded.
static const size_t READ_CAPTURE_MAX = 2048;
static const size_t READ_RAW_MAX = 1024;

// Room geometry, purely visual (3D outline/framing), persisted in flash --
// identical to the 6001B's final RoomConfig, including the two independent
// display rotations (HLK XY view / Plots top view, 0-3 x 90 deg) and the
// 5 m / 3 m limits. Fields are only ever APPENDED, never reordered, so a
// saved blob stays loadable.
struct RoomConfig {
  float width = 4.0f;
  float length = 4.0f;
  float height = 2.5f;
  float radar_height = 1.5f;
  float wall_offset_m = 2.0f;
  uint8_t mount = 0;
  uint8_t hlk_view_rotation = 0;
  uint8_t top_view_rotation = 0;
};
static const float ROOM_MAX_WIDTH_LENGTH_M = 5.0f;
static const float ROOM_MAX_HEIGHT_M = 3.0f;

// Radar-side settings -- the commands this radar firmware actually accepts
// (see the header comment). Ranges: the documented one where a document
// covers the command (DPKTHF = the manual's AT+DPKTH, 1-9; RANGE 10-500;
// HEIGHT 250-320 per the MinewSemi datasheet; HEATIME 10-999), otherwise
// stated as an assumption. The ESP32 is the source of truth: every setting
// is re-sent after each (re)init.
static const uint8_t RADAR_SENS_MIN = 1;  // AT+DPKTHF: manual's AT+DPKTH range
static const uint8_t RADAR_SENS_MAX = 9;  // AT+DPKTHN: undocumented, same range assumed
static const uint16_t RADAR_RANGE_CM_MIN = 10;
static const uint16_t RADAR_RANGE_CM_MAX = 500;
static const uint16_t RADAR_HEIGHT_CM_MIN = 250;  // AT+HEIGHT, MinewSemi datasheet §7
static const uint16_t RADAR_HEIGHT_CM_MAX = 320;
static const uint16_t RADAR_HRANGE_CM_MIN = 50;  // AT+HRANGE: undocumented -- the
static const uint16_t RADAR_HRANGE_CM_MAX = 500;  // manual's AT+HEIGHTD range assumed
static const uint16_t RADAR_HEATIME_S_MIN = 10;
static const uint16_t RADAR_HEATIME_S_MAX = 999;
// Single rectangular detection zone: 4 independent signed offsets from the
// radar origin, intersected with the AT+RANGE circle (manual §6 diagram).
static const int16_t RADAR_ZONE_POS_MIN_CM = 20;
static const int16_t RADAR_ZONE_POS_MAX_CM = 500;
static const int16_t RADAR_ZONE_NEG_MIN_CM = -500;
static const int16_t RADAR_ZONE_NEG_MAX_CM = -20;
// Zone disabled = bounds pushed to the maximum (+-500 cm, accepted on the
// real module), so only the AT+RANGE circle limits detection -- the radar
// always applies SOME bounds (+-300 cm stored on arrival), sending nothing
// would leave that hidden restriction in place.
static const int16_t RADAR_ZONE_DISABLED_BOUND_CM = 500;

struct RadarSettings {
  uint8_t sens_far = 4;       // AT+DPKTHF (DPKF), larger = less sensitive (manual, AT+DPKTH)
  uint8_t sens_near = 5;      // AT+DPKTHN (DPKN), value found on arrival
  uint16_t range_cm = 450;    // AT+RANGE (Range), ground circle radius
  uint16_t height_cm = 270;   // AT+HEIGHT (Height), installation height
  uint16_t hrange_cm = 200;   // AT+HRANGE (Hrange), value found on arrival
  uint16_t heartbeat_s = 60;  // AT+HEATIME (Heart_Time)
  bool zone_enabled = false;
  int16_t zone_x_neg = -300;  // AT+XNegaD (XdetectionN) -- radar's own value on arrival
  int16_t zone_x_pos = 300;   // AT+XPosiD (XdetectionP)
  int16_t zone_y_neg = -300;  // AT+YNegaD (YdetectionN)
  int16_t zone_y_pos = 300;   // AT+YPosiD (YdetectionP)
};

// Values parsed out of the last AT+READ block -- has_* false = key absent
// from the reply.
struct RadarLiveRead {
  bool has_sens_far = false, has_sens_near = false, has_range = false, has_height = false;
  bool has_hrange = false, has_heartbeat = false, has_zone = false;
  int sens_far = 0, sens_near = 0, range_cm = 0, height_cm = 0, hrange_cm = 0, heartbeat_s = 0;
  int zone_x_neg = 0, zone_x_pos = 0, zone_y_neg = 0, zone_y_pos = 0;
  std::string version;
};

class HlkLd6001aComponent final : public Component, public uart::UARTDevice {
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(has_target)
#endif
#ifdef USE_SENSOR
  SUB_SENSOR(target_count)
  SUB_SENSOR(recovery_count)
#endif

 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

#ifdef USE_SENSOR
  void set_target_x_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_y_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_z_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_vx_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_vy_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_vz_sensor(uint8_t index, sensor::Sensor *s);
  void set_target_id_sensor(uint8_t index, sensor::Sensor *s);
#endif

 protected:
  enum class TlvFraming : uint8_t { UNKNOWN, NO_CHECK, WITH_CHECK };
  enum class TlvVerdict : uint8_t { ACCEPT, WAIT, REJECT };

  void process_tlv_frame_(const uint8_t *frame, uint32_t point_len, uint32_t track_len, size_t people_start);
  // Same decision rules as testing/radar_protocol_tlv.py::decide() -- keep
  // the two in sync.
  TlvVerdict decide_tlv_framing_(size_t length, uint8_t xor_check, size_t &consumed);
  void send_reinit_sequence_();
  void check_watchdog_();
  void start_http_server_();
  static esp_err_t http_handle_root_(httpd_req_t *req);
  static esp_err_t http_handle_three_js_(httpd_req_t *req);
  static esp_err_t http_handle_orbit_controls_js_(httpd_req_t *req);
  static esp_err_t http_handle_targets_json_(httpd_req_t *req);
  static esp_err_t http_handle_room_get_(httpd_req_t *req);
  static esp_err_t http_handle_room_post_(httpd_req_t *req);
  std::string room_config_json_() const;

  // Enqueues AT+STOP, every saved setting, then AT+READ; every call site
  // follows with start_after_settings_queue_drains_() (AT+DEBUG=2/AT+START).
  void apply_radar_settings_();
  // Set by http_handle_settings_post_ (httpd task); loop() does the real
  // call -- UART writes must stay on the main task (6001B finding
  // 2026-09-06: writes from the httpd task never reached the wire).
  volatile bool pending_apply_radar_settings_ = false;
  std::string radar_settings_json_() const;
  static esp_err_t http_handle_settings_get_(httpd_req_t *req);
  static esp_err_t http_handle_settings_post_(httpd_req_t *req);
  // Advanced Commands backend, non-blocking (POST queues, the page polls
  // /at_command_result) -- same design as the 6001B.
  static esp_err_t http_handle_at_command_post_(httpd_req_t *req);
  static esp_err_t http_handle_at_command_result_get_(httpd_req_t *req);
  // "Redemarrer le radar" button -- same full cycle as the watchdog recovery.
  // Hands over to loop() through pending_radar_restart_: the 6001B called
  // send_reinit_sequence_() straight from the httpd task, which writes to
  // the UART and touches the command queue off the main task.
  static esp_err_t http_handle_radar_restart_post_(httpd_req_t *req);
  volatile bool pending_radar_restart_ = false;

  // AT+ command queue: one command in flight, advanced on a real AT+OK /
  // AT+ERR / "Save Para Fail" reply (or the AT+READ block) or after
  // AT_COMMAND_ACK_TIMEOUT_MS.
  struct PendingAtCommand {
    std::string data;  // includes trailing '\n'
    std::function<void(bool ok, const std::string &raw_response)> on_result;
  };
  void enqueue_at_command_(const std::string &cmd_with_newline,
                            std::function<void(bool ok, const std::string &raw_response)> on_result = nullptr);
  void service_at_command_queue_();
  void resolve_in_flight_(bool ok, const std::string &raw);
  void start_after_settings_queue_drains_(uint32_t waited_ms);
  void handle_at_response_line_(const std::string &line);
  std::deque<PendingAtCommand> at_command_queue_;
  bool at_command_in_flight_ = false;
  uint32_t at_command_sent_millis_ = 0;

  // AT+READ reply capture (see the header comment) -- active only while an
  // AT+READ is the in-flight command.
  void feed_read_capture_(const uint8_t *data, size_t len);
  void check_read_capture_();
  void parse_read_response_(const std::string &text);
  bool read_capture_active_ = false;
  bool read_capture_prev_a3_ = false;
  std::string read_capture_;

  // Cross-task handoff for /at_command -- single mailbox, 409 when busy.
  volatile bool http_at_command_done_ = false;
  volatile bool http_at_command_ok_ = false;
  std::string http_at_command_response_;
  volatile bool http_at_command_busy_ = false;

  std::vector<uint8_t> buffer_;
  uint32_t last_frame_millis_ = 0;
  uint32_t last_recovery_millis_ = 0;
  uint32_t recovery_count_ = 0;
  uint32_t last_publish_millis_ = 0;

  // Target count debounce state (see TARGET_COUNT_INCREASE_DEBOUNCE_MS).
  uint8_t published_target_count_ = 0;
  uint8_t pending_target_count_ = 0;
  uint32_t pending_target_count_since_ = 0;

  // TLV framing state (sticky, re-learned whenever a full decision is
  // possible -- see decide_tlv_framing_()) and counters for /hlk_targets.json.
  TlvFraming tlv_framing_ = TlvFraming::UNKNOWN;
  uint32_t tlv_frame_count_ = 0;
  uint32_t tlv_rejected_count_ = 0;
  uint32_t last_point_len_ = 0;
  bool logged_first_point_frame_ = false;
  bool logged_first_person_frame_ = false;
  uint16_t logged_old_frame_types_ = 0;  // bit n = first 55 AA frame of TYPE n already logged

  httpd_handle_t http_server_ = nullptr;
  bool http_server_start_attempted_ = false;
  bool radar_settings_start_attempted_ = false;
  ESPPreferenceObject room_config_pref_;
  RoomConfig room_config_;
  ESPPreferenceObject radar_settings_pref_;
  RadarSettings radar_settings_;

  // "Etat radar" ground truth -- last successfully captured AT+READ block.
  bool live_read_seen_ = false;
  uint32_t live_read_millis_ = 0;
  std::string live_read_raw_;
  RadarLiveRead live_read_;

  uint8_t latest_num_people_ = 0;
  uint32_t latest_id_[MAX_TARGETS] = {};
  float latest_x_[MAX_TARGETS] = {};
  float latest_y_[MAX_TARGETS] = {};
  float latest_z_[MAX_TARGETS] = {};
  float latest_vx_[MAX_TARGETS] = {};
  float latest_vy_[MAX_TARGETS] = {};
  float latest_vz_[MAX_TARGETS] = {};

  uint16_t latest_point_count_ = 0;
  float latest_point_x_[MAX_POINTS] = {};
  float latest_point_y_[MAX_POINTS] = {};
  float latest_point_z_[MAX_POINTS] = {};
  float latest_point_d_[MAX_POINTS] = {};

#ifdef USE_SENSOR
  sensor::Sensor *target_x_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_y_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_z_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_vx_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_vy_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_vz_sensors_[MAX_TARGETS] = {};
  sensor::Sensor *target_id_sensors_[MAX_TARGETS] = {};
#endif
};

}  // namespace esphome::hlk_ld6001a

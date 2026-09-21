// Custom ESPHome external component for the Hi-Link HLK-LD6001A 60GHz
// multi-target mmWave radar. Direct port of the HLK-LD6001B project's
// component (same XIAO ESP32-S3 board, same external-component pattern,
// same 3D-viewer/AT-command-queue/watchdog architecture) -- see
// ../../../PLAN.md for the full list of protocol differences that drove
// every change below, and ../../../PROTOCOL.md for the field reference.
//
// The single biggest architectural difference from the 6001B: this module
// has only ONE source of target position/velocity/ID (AT+DEBUG=3). The
// 6001B's AT+DEBUG=0 default protocol already carries X/Y/Z and AT+DEBUG=2
// is additive (own ID/velocity stream, matched by nearest position since
// there's no shared key). Here AT+DEBUG=0 only ever gives a person COUNT,
// no coordinates -- so this component starts directly in AT+DEBUG=3 and
// there is no second stream to reconcile, no matchDebug2Targets()
// equivalent, no separate resp/heart/gesture sensors (this protocol
// doesn't provide them at all, unlike the 6001B where they exist but are
// always 0 on every unit tested).
//
// Frame layout (AT+DEBUG=3, the only frame this component decodes):
//   HEAD        8 bytes   fixed 01 02 03 04 05 06 07 08
//   LENGTH      4 bytes   uint32 LE, HEAD..end of person records -- does
//                         NOT include the trailing CHECK byte (verified by
//                         hand against the manual's own worked example,
//                         see PLAN.md "Journal Phase 0" -- a real frame on
//                         the wire is LENGTH+1 bytes).
//   FRAME       4 bytes   uint32 LE, frame counter (not currently used)
//   TLV1        4 bytes   uint32 LE, expected 1 (point-cloud marker)
//   POINTLEN    4 bytes   uint32 LE, bytes of point-cloud data to skip.
//                         Manual claims "always 0" -- decoded dynamically
//                         anyway, direct lesson from the 6001B project
//                         where that same assumption was field-proven
//                         false (see PLAN.md).
//   <POINTLEN bytes, not decoded>
//   TLV2        4 bytes   uint32 LE, expected 2 (person/track marker)
//   TRACKLEN    4 bytes   uint32 LE, num_persons = TRACKLEN / 32
//   <TRACKLEN bytes, 32-byte person records>:
//       Q       4 bytes   uint32 LE, reserved
//       ID      4 bytes   uint32 LE, persistent target ID
//       X,Y,Z            float32 LE, metres
//       Vx,Vy,Vz         float32 LE, m/s
//   CHECK       1 byte    XOR of FRAME (4 bytes) + every byte of the
//                         person records above -- NOT over
//                         LENGTH/TLV1/POINTLEN/TLV2/TRACKLEN, NOT over any
//                         point-cloud bytes. Confirmed by hand against the
//                         manual's own worked example (0xCC) -- see
//                         testing/test_protocol_debug3.py. VALIDATED here
//                         (frame rejected on mismatch), unlike the 6001B's
//                         DEBUG2 checksum which was never confirmed and so
//                         never checked.
//
// The default AT+DEBUG=0 protocol (55 AA framing, TYPE=0x04 = person count
// only, no coordinates) is recognized on the wire (checksum-validated, same
// XOR algorithm as the 6001B's normal protocol -- confirmed against the
// manual's own worked example) so it can be cleanly consumed and feed the
// watchdog, but its payload is never decoded: AT+DEBUG=3 already provides
// a person count (TRACKLEN/32) that makes it redundant, and it carries no
// coordinates anyway.
//
// Heartbeat (TYPE 0x02) status: AT+HEATIME (heartbeat interval, 10-999s)
// IS documented for this module, but neither manual documents the
// heartbeat FRAME's byte layout (no field table, no worked example, unlike
// the 6001B). This component therefore does not attempt to decode one --
// see AT_READ_POLL_INTERVAL_MS below for the "Etat radar" replacement
// mechanism, and PLAN.md for the precise Phase 1 test that will confirm or
// rule out a real heartbeat frame on the wire.
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

#include <esp_http_server.h>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace esphome::hlk_ld6001a {

// Protocol supports up to 10 tracked people (manual Hi-Link, "Product
// features"); 6 covers realistic room occupancy and matches the web UI's
// 6-cell target table -- same reasoning and same value as the 6001B
// project, explicit user decision 2026-09-09 (PLAN.md, "Points ouverts").
static const uint8_t MAX_TARGETS = 6;

// Installation geometry filter -- identical reasoning to the 6001B
// (manual Hi-Link recommends ceiling mount at 2.5-3.0m; no real target can
// physically be within 30cm of the radar itself).
static const float MIN_TARGET_DISTANCE_M = 0.30f;

// Self-healing watchdog -- same thresholds as the 6001B. Generous even
// against the MS72SF1 datasheet's claimed <=30ms processing cycle (vs. the
// 6001B's ~100ms): a truly silent link for 90s is always abnormal
// regardless of which module's cadence is in play.
static const uint32_t WATCHDOG_TIMEOUT_MS = 90000;
static const uint32_t WATCHDOG_COOLDOWN_MS = 30000;

// AT+ command queue ack timeout -- same value as the 6001B (ported from
// Devristo's esphome-hlk-ld6001a CommandQueue there too).
static const uint32_t AT_COMMAND_ACK_TIMEOUT_MS = 5000;

// Same reasoning as the 6001B: apply_radar_settings_() enqueues its whole
// batch in one shot (3 settings + up to 4 zone commands = 7, smaller than
// the 6001B's 11 since there are fewer confirmed settings for this
// module), this leaves generous headroom above that for console commands
// on top.
static const size_t MAX_AT_COMMAND_QUEUE_SIZE = 32;

// No heartbeat frame decoded (see header comment) -- "Etat radar" instead
// polls AT+READ periodically via the same ack/timeout-gated command queue
// used for everything else, and shows its raw text response verbatim (no
// field-by-field parser: the response format is undocumented and reported
// to vary by firmware version, see PROTOCOL.md). 15s is an arbitrary
// first-pass choice, not derived from any documented interval -- cheap to
// retune once real device behavior (response latency, whether it disrupts
// AT+DEBUG=3 streaming) is observed in Phase 1.
static const uint32_t AT_READ_POLL_INTERVAL_MS = 15000;

// Rate-limits HA sensor publish_state() calls (NOT the raw
// /hlk_targets.json feed for the 3D viewer, which stays unthrottled/raw
// like the 6001B's own "never debounced" philosophy) -- explicit user
// decision 2026-09-09 (PLAN.md, "Points ouverts", point 4), matching
// Devristo's own reasoning: this module's claimed <=30ms processing cycle
// (vs. the 6001B's ~100ms) would otherwise flood Home Assistant's
// recorder. Value explicitly flagged by the user as "to revisit during
// testing" -- not a value derived from any measurement yet.
static const uint32_t PUBLISH_THROTTLE_MS = 1000;

// Room geometry -- purely visual/record-keeping, identical to the 6001B
// (see that project's hlk_ld6001b.h for the full field-by-field rationale,
// unchanged here since none of it is protocol-specific).
struct RoomConfig {
  float width = 4.0f;
  float length = 4.0f;
  float height = 2.5f;
  float radar_height = 1.5f;
  float wall_offset_m = 2.0f;
  uint8_t mount = 0;
};
static const float ROOM_MAX_WIDTH_LENGTH_M = 4.5f;
static const float ROOM_MAX_HEIGHT_M = 2.5f;

// Radar-side settings, backed by AT+ commands confirmed in the Hi-Link
// manual V1.1 section 6 (and cross-checked against Devristo's tested
// component where the manual itself is ambiguous -- see PROTOCOL.md and
// PLAN.md "Journal Phase 0"). Persisted here for the same reason as the
// 6001B: no documented guarantee the radar keeps these across its own
// power cycle, and our own AT+RESET-based recovery reboots it
// independently of the ESP32.
//
// Deliberately NOT included in this first port (no confirmed
// range/default in either manual, no user decision taken yet -- see
// PROTOCOL.md "documentées mais non exposées"): AT+Moving, AT+Static,
// AT+Exit.
static const uint8_t RADAR_DPKTH_MIN = 1;
static const uint8_t RADAR_DPKTH_MAX = 9;
static const uint16_t RADAR_RANGE_CM_MIN = 10;
static const uint16_t RADAR_RANGE_CM_MAX = 500;
static const uint16_t RADAR_HEIGHTD_CM_MIN = 50;
static const uint16_t RADAR_HEIGHTD_CM_MAX = 500;
// Zone bounds: the module defines its rectangular zone by 4 independent
// signed offsets from its own origin (not two free corners like the
// 6001B's WINxRANGE) -- AT+XPosi/AT+YPosi are positive-only, AT+XNega/
// AT+YNega are negative-only, per the manual's own documented ranges.
static const int16_t RADAR_ZONE_POS_MIN_CM = 20;
static const int16_t RADAR_ZONE_POS_MAX_CM = 500;
static const int16_t RADAR_ZONE_NEG_MIN_CM = -500;
static const int16_t RADAR_ZONE_NEG_MAX_CM = -20;

struct RadarSettings {
  uint8_t sensitivity = 4;     // AT+DPKTH, manual default (larger = less sensitive)
  uint16_t range_cm = 450;     // AT+RANGE, manual default
  uint16_t height_d_cm = 300;  // AT+HEIGHTD, manual default
  bool zone_enabled = false;
  int16_t zone_x_neg = -450;  // AT+XNega, manual default (as AT+XNegaD)
  int16_t zone_x_pos = 450;   // AT+XPosi, manual default (as AT+XPosiD)
  int16_t zone_y_neg = -450;  // AT+YNega, manual default (as AT+YNegaD)
  int16_t zone_y_pos = 450;   // AT+YPosi, manual default (as AT+YPosiD)
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
  void process_debug3_frame_(const uint8_t *frame, size_t len);
  void send_reinit_sequence_();
  void check_watchdog_();
  void poll_at_read_();
  void start_http_server_();
  static esp_err_t http_handle_root_(httpd_req_t *req);
  static esp_err_t http_handle_targets_json_(httpd_req_t *req);
  static esp_err_t http_handle_room_get_(httpd_req_t *req);
  static esp_err_t http_handle_room_post_(httpd_req_t *req);
  std::string room_config_json_() const;

  void apply_radar_settings_();
  bool pending_apply_radar_settings_ = false;
  std::string radar_settings_json_() const;
  static esp_err_t http_handle_settings_get_(httpd_req_t *req);
  static esp_err_t http_handle_settings_post_(httpd_req_t *req);
  static esp_err_t http_handle_at_command_post_(httpd_req_t *req);
  static esp_err_t http_handle_at_command_result_get_(httpd_req_t *req);

  // AT+ command queue -- verbatim same design as the 6001B project
  // (itself ported from Devristo's esphome-hlk-ld6001a CommandQueue): one
  // command "in flight" at a time, advanced only on a real AT+OK/AT+ERR
  // ack or after AT_COMMAND_ACK_TIMEOUT_MS.
  struct PendingAtCommand {
    std::string data;  // includes trailing '\n'
    std::function<void(bool ok, const std::string &raw_response)> on_result;
  };
  void enqueue_at_command_(const std::string &cmd_with_newline,
                            std::function<void(bool ok, const std::string &raw_response)> on_result = nullptr);
  void service_at_command_queue_();
  void start_after_settings_queue_drains_(uint32_t waited_ms);
  void handle_at_response_line_(const std::string &line);
  std::deque<PendingAtCommand> at_command_queue_;
  bool at_command_in_flight_ = false;
  uint32_t at_command_sent_millis_ = 0;

  // Cross-task handoff for http_handle_at_command_post_ -- see the 6001B
  // project's identical fields for the full rationale (single mailbox,
  // 409-on-busy backstop for a second tab/session).
  volatile bool http_at_command_done_ = false;
  volatile bool http_at_command_ok_ = false;
  std::string http_at_command_response_;
  volatile bool http_at_command_busy_ = false;

  std::vector<uint8_t> buffer_;
  uint32_t last_frame_millis_ = 0;
  uint32_t last_recovery_millis_ = 0;
  uint32_t recovery_count_ = 0;
  uint32_t last_at_read_poll_millis_ = 0;
  uint32_t last_publish_millis_ = 0;

  httpd_handle_t http_server_ = nullptr;
  bool http_server_start_attempted_ = false;
  bool radar_settings_start_attempted_ = false;
  ESPPreferenceObject room_config_pref_;
  RoomConfig room_config_;
  ESPPreferenceObject radar_settings_pref_;
  RadarSettings radar_settings_;
  bool radar_settings_applied_ = false;

  // "Etat radar" ground truth -- raw AT+READ response text, not parsed
  // field-by-field (see header comment and PROTOCOL.md). live_read_seen_
  // becomes true after the first successful (ok=true) AT+READ reply;
  // live_read_millis_ lets the web UI flag a stale/no-longer-responding
  // radar (see MAINTENANCE.md) instead of showing a frozen old response
  // forever.
  bool live_read_seen_ = false;
  std::string live_read_raw_;
  uint32_t live_read_millis_ = 0;

  uint8_t latest_num_people_ = 0;
  uint32_t latest_id_[MAX_TARGETS] = {};
  float latest_x_[MAX_TARGETS] = {};
  float latest_y_[MAX_TARGETS] = {};
  float latest_z_[MAX_TARGETS] = {};
  float latest_vx_[MAX_TARGETS] = {};
  float latest_vy_[MAX_TARGETS] = {};
  float latest_vz_[MAX_TARGETS] = {};

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

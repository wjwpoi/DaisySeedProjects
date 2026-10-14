#include "cJSON.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "parameters.hpp"
#include "protocol.hpp"
#include <cstdio>
#include <cstring>

using namespace seedfx;
static SemaphoreHandle_t state_lock, send_lock, ack_signal;
static Frame latest_state;
static int64_t state_time = 0;
static uint32_t bridge_session = 0;
static uint16_t next_seq = 1, pending_seq = 0;
static uint8_t pending_result = LINK_TIMEOUT;
constexpr uart_port_t kUart = UART_NUM_1;
static void send_frame(const Frame &f) {
  uint8_t wire[kMaxFrame];
  size_t n = encode(f, wire, sizeof(wire));
  xSemaphoreTake(send_lock, portMAX_DELAY);
  uart_write_bytes(kUart, wire, n);
  xSemaphoreGive(send_lock);
}
static void serial_task(void *) {
  Decoder decoder;
  Frame frame;
  uint8_t bytes[128];
  int64_t last_poll = 0, last_hello = -1000000;
  while (true) {
    int n = uart_read_bytes(kUart, bytes, sizeof(bytes), pdMS_TO_TICKS(10));
    for (int i = 0; i < n; ++i)
      if (decoder.feed(bytes[i], frame)) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if (frame.type == STATE &&
            frame.size == PARAM_COUNT * 4 + kKnobCount + 28 &&
            get32(frame.data + PARAM_COUNT * 4 + kKnobCount + 24) ==
                bridge_session) {
          latest_state = frame;
          state_time = esp_timer_get_time();
        }
        if (frame.type == ACK && frame.size == 1 && frame.seq == pending_seq) {
          pending_result = frame.data[0];
          xSemaphoreGive(ack_signal);
        }
        xSemaphoreGive(state_lock);
      }
    auto now = esp_timer_get_time();
    if (now - last_hello >= 1000000) {
      Frame hello;
      hello.type = HELLO;
      hello.size = 4;
      put32(hello.data, bridge_session);
      send_frame(hello);
      last_hello = now;
    }
    if (now - last_poll >= 100000) {
      Frame poll;
      poll.type = GET_STATE;
      send_frame(poll);
      last_poll = now;
    }
  }
}
static esp_err_t json_reply(httpd_req_t *req, cJSON *json,
                            const char *status = "200 OK") {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char *text = cJSON_PrintUnformatted(json);
  if (!text) {
    cJSON_Delete(json);
    return ESP_ERR_NO_MEM;
  }
  auto r = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
  cJSON_free(text);
  cJSON_Delete(json);
  return r;
}
static esp_err_t state_handler(httpd_req_t *req) {
  Frame f;
  int64_t timestamp;
  xSemaphoreTake(state_lock, portMAX_DELAY);
  f = latest_state;
  timestamp = state_time;
  xSemaphoreGive(state_lock);
  bool connected = timestamp && esp_timer_get_time() - timestamp < 1500000;
  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "connected", connected);
  cJSON_AddNumberToObject(json, "ageMs",
                          timestamp ? (esp_timer_get_time() - timestamp) / 1000
                                    : -1);
  if (connected) {
    auto *values = cJSON_AddArrayToObject(json, "values");
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
      cJSON_AddItemToArray(values,
                           cJSON_CreateNumber(getfloat(f.data + 4 * i)));
    unsigned off = PARAM_COUNT * 4;
    auto *knobs = cJSON_AddArrayToObject(json, "knobs");
    for (unsigned i = 0; i < kKnobCount; ++i)
      cJSON_AddItemToArray(knobs, cJSON_CreateNumber(f.data[off++]));
    cJSON_AddNumberToObject(json, "preset", f.data[off++]);
    cJSON_AddBoolToObject(json, "bypassed", f.data[off++]);
    cJSON_AddBoolToObject(json, "tuning", f.data[off++]);
    uint8_t flags = f.data[off++];
    cJSON_AddBoolToObject(json, "dirty", flags & 1);
    cJSON_AddBoolToObject(json, "busy", flags & 2);
    cJSON_AddBoolToObject(json, "clipped", flags & 4);
    cJSON_AddBoolToObject(json, "storageError", flags & 8);
    cJSON_AddNumberToObject(json, "pitch", getfloat(f.data + off));
    cJSON_AddNumberToObject(json, "cpuAverage", getfloat(f.data + off + 4));
    cJSON_AddNumberToObject(json, "cpuMaximum", getfloat(f.data + off + 8));
    cJSON_AddNumberToObject(json, "inputPeak", getfloat(f.data + off + 12));
    cJSON_AddNumberToObject(json, "outputPeak", getfloat(f.data + off + 16));
  }
  return json_reply(req, json);
}
static bool integer(cJSON *value, int low, int high) {
  return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
         value->valuedouble >= low && value->valuedouble <= high &&
         std::floor(value->valuedouble) == value->valuedouble;
}
static esp_err_t command_handler(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len > 512) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid command size");
    return ESP_OK;
  }
  char body[513]{};
  size_t have = 0;
  while (have < size_t(req->content_len)) {
    int n = httpd_req_recv(req, body + have, req->content_len - have);
    if (n <= 0)
      return ESP_FAIL;
    have += n;
  }
  auto *json = cJSON_ParseWithLength(body, have);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_OK;
  }
  auto *op = cJSON_GetObjectItemCaseSensitive(json, "op");
  Frame frame;
  bool valid = cJSON_IsString(op);
  if (valid && std::strcmp(op->valuestring, "set") == 0) {
    auto *id = cJSON_GetObjectItemCaseSensitive(json, "id");
    auto *value = cJSON_GetObjectItemCaseSensitive(json, "value");
    valid = integer(id, 0, PARAM_COUNT - 1) && cJSON_IsNumber(value) &&
            valid_value(id->valueint, float(value->valuedouble));
    if (valid) {
      frame.type = SET_PARAM;
      frame.size = 5;
      frame.data[0] = id->valueint;
      putfloat(frame.data + 1, float(value->valuedouble));
    }
  } else if (valid && (std::strcmp(op->valuestring, "load") == 0 ||
                       std::strcmp(op->valuestring, "save") == 0)) {
    auto *slot = cJSON_GetObjectItemCaseSensitive(json, "preset");
    valid = integer(slot, 0, kPresetCount - 1);
    if (valid) {
      frame.type =
          std::strcmp(op->valuestring, "load") == 0 ? LOAD_PRESET : SAVE_PRESET;
      frame.size = 1;
      frame.data[0] = slot->valueint;
    }
  } else if (valid && std::strcmp(op->valuestring, "knobs") == 0) {
    auto *mapping = cJSON_GetObjectItemCaseSensitive(json, "knobs");
    valid = cJSON_IsArray(mapping) && cJSON_GetArraySize(mapping) == 6;
    for (unsigned i = 0; valid && i < 6; ++i) {
      auto *id = cJSON_GetArrayItem(mapping, i);
      valid =
          integer(id, 0, LEVEL) && id->valueint != MODEL && id->valueint != CAB;
      if (valid)
        frame.data[i] = id->valueint;
    }
    frame.type = SET_KNOBS;
    frame.size = 6;
  } else if (valid && (std::strcmp(op->valuestring, "bypass") == 0 ||
                       std::strcmp(op->valuestring, "tuner") == 0)) {
    auto *enabled = cJSON_GetObjectItemCaseSensitive(json, "enabled");
    valid = cJSON_IsBool(enabled);
    frame.type = std::strcmp(op->valuestring, "bypass") == 0 ? BYPASS : TUNER;
    frame.size = 1;
    frame.data[0] = cJSON_IsTrue(enabled);
  } else if (valid && std::strcmp(op->valuestring, "tap") == 0) {
    frame.type = TAP;
    frame.size = 0;
  } else
    valid = false;
  cJSON_Delete(json);
  if (!valid) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Command out of range");
    return ESP_OK;
  }
  xSemaphoreTake(state_lock, portMAX_DELAY);
  bool online = state_time && esp_timer_get_time() - state_time < 1500000;
  frame.seq = next_seq++;
  if (!next_seq)
    next_seq = 1;
  pending_seq = frame.seq;
  pending_result = LINK_TIMEOUT;
  xSemaphoreTake(ack_signal, 0);
  xSemaphoreGive(state_lock);
  if (!online) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Audio board disconnected");
    return ESP_OK;
  }
  send_frame(frame);
  bool acknowledged = xSemaphoreTake(ack_signal, pdMS_TO_TICKS(2000)) == pdTRUE;
  xSemaphoreTake(state_lock, portMAX_DELAY);
  unsigned result =
      acknowledged ? unsigned(pending_result) : unsigned(LINK_TIMEOUT);
  pending_seq = 0;
  xSemaphoreGive(state_lock);
  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", result == OK);
  cJSON_AddNumberToObject(response, "result", result);
  cJSON_AddNumberToObject(response, "sequence", frame.seq);
  return json_reply(req, response,
                    result == OK             ? "200 OK"
                    : result == LINK_TIMEOUT ? "504 Gateway Timeout"
                                             : "409 Conflict");
}
extern const uint8_t html_start[] asm("_binary_index_html_start"),
    html_end[] asm("_binary_index_html_end");
extern const uint8_t css_start[] asm("_binary_style_css_start"),
    css_end[] asm("_binary_style_css_end");
extern const uint8_t app_start[] asm("_binary_app_js_start"),
    app_end[] asm("_binary_app_js_end");
extern const uint8_t api_start[] asm("_binary_api_js_start"),
    api_end[] asm("_binary_api_js_end");
extern const uint8_t schema_start[] asm("_binary_schema_json_start"),
    schema_end[] asm("_binary_schema_json_end");
struct Asset {
  const uint8_t *begin;
  const uint8_t *end;
  const char *type;
};
static const Asset assets[] = {
    {html_start, html_end, "text/html; charset=utf-8"},
    {css_start, css_end, "text/css"},
    {app_start, app_end, "text/javascript"},
    {api_start, api_end, "text/javascript"},
    {schema_start, schema_end, "application/json"}};
static esp_err_t asset_handler(httpd_req_t *req) {
  auto *a = static_cast<const Asset *>(req->user_ctx);
  httpd_resp_set_type(req, a->type);
  return httpd_resp_send(req, reinterpret_cast<const char *>(a->begin),
                         a->end - a->begin - 1);
}
extern "C" void app_main() {
  auto status = nvs_flash_init();
  if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
      status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    status = nvs_flash_init();
  }
  ESP_ERROR_CHECK(status);
  state_lock = xSemaphoreCreateMutex();
  send_lock = xSemaphoreCreateMutex();
  ack_signal = xSemaphoreCreateBinary();
  uart_config_t uc{};
  uc.baud_rate = 230400;
  uc.data_bits = UART_DATA_8_BITS;
  uc.parity = UART_PARITY_DISABLE;
  uc.stop_bits = UART_STOP_BITS_1;
  uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uc.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(kUart, 2048, 2048, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(kUart, &uc));
  // Seeed XIAO ESP32-C3: D6/GPIO21=TX, D7/GPIO20=RX, 3.3 V logic.
  ESP_ERROR_CHECK(
      uart_set_pin(kUart, 21, 20, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  wifi_config_t wc{};
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  std::snprintf(reinterpret_cast<char *>(wc.ap.ssid), sizeof(wc.ap.ssid),
                "SeedFX-%02X%02X", mac[4], mac[5]);
  std::strcpy(reinterpret_cast<char *>(wc.ap.password), "seedfx123");
  wc.ap.ssid_len = std::strlen(reinterpret_cast<char *>(wc.ap.ssid));
  wc.ap.channel = 6;
  wc.ap.max_connection = 2;
  wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  bridge_session = esp_random();
  if (!bridge_session)
    bridge_session = 1;
  xTaskCreate(serial_task, "seedfx_uart", 4096, nullptr, 5, nullptr);
  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.stack_size = 8192;
  hc.max_uri_handlers = 8;
  httpd_handle_t server;
  ESP_ERROR_CHECK(httpd_start(&server, &hc));
  const char *paths[] = {"/", "/style.css", "/app.js", "/api.js",
                         "/schema.json"};
  for (unsigned i = 0; i < 5; ++i) {
    httpd_uri_t u{};
    u.uri = paths[i];
    u.method = HTTP_GET;
    u.handler = asset_handler;
    u.user_ctx = const_cast<Asset *>(&assets[i]);
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &u));
  }
  httpd_uri_t get{};
  get.uri = "/api/state";
  get.method = HTTP_GET;
  get.handler = state_handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &get));
  httpd_uri_t post{};
  post.uri = "/api/command";
  post.method = HTTP_POST;
  post.handler = command_handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &post));
}

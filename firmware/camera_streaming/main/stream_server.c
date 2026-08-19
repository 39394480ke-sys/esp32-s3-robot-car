#include "stream_server.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"

#include "camera_safety_check.h"
#include "jpeg_validator.h"
#include "stream_protocol.h"
#include "wifi_manager.h"

static const char *TAG = "stream_server";

#define STREAM_TASK_STACK_SIZE 6144U
#define STREAM_TASK_PRIORITY 5U
#define STREAM_HEADER_CONSENSUS_FRAMES 3U
#define STREAM_PACKET_OVERHEAD 512U

static const char VIEWER_HTML[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>robot-01 camera</title><style>"
    ":root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101214;color:#f4f5f5}"
    "*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#101214}"
    "header{height:56px;padding:0 18px;display:flex;align-items:center;gap:16px;"
    "border-bottom:1px solid #30363a;background:#171a1d}"
    "h1{font-size:17px;margin:0;font-weight:650}#state{margin-left:auto;color:#82d9a6}"
    "main{width:min(100%,980px);margin:0 auto;padding:16px}"
    ".video{background:#050606;aspect-ratio:4/3;display:grid;place-items:center;overflow:hidden}"
    "img{width:100%;height:100%;object-fit:contain;display:block}"
    ".bar{display:flex;align-items:center;gap:12px;min-height:54px;border-bottom:1px solid #30363a}"
    ".metric{font-variant-numeric:tabular-nums;color:#c9d0d3}.metric b{color:#fff}"
    "button{margin-left:auto;border:1px solid #465057;border-radius:6px;background:#252b2f;"
    "color:#fff;padding:8px 12px;font:inherit;cursor:pointer}button+button{margin-left:0}"
    "@media(max-width:600px){main{padding:0}.bar{padding:0 12px;gap:8px;flex-wrap:wrap}"
    ".metric{font-size:13px}header{padding:0 12px}}"
    "</style></head><body><header><h1 id=\"robot\">robot-01</h1>"
    "<span id=\"state\">CONNECTING</span></header><main>"
    "<div class=\"video\"><img id=\"feed\" alt=\"Robot camera stream\"></div>"
    "<div class=\"bar\"><span class=\"metric\"><b id=\"fps\">0.0</b> FPS</span>"
    "<span class=\"metric\" id=\"resolution\">320x240</span>"
    "<span class=\"metric\"><b id=\"rssi\">--</b> dBm</span>"
    "<button id=\"pause\">Pause</button><button id=\"reconnect\">Reconnect</button>"
    "</div></main><script>"
    "const feed=document.getElementById('feed'),state=document.getElementById('state');"
    "let paused=false,retries=0,retryTimer=0,lastFrames=0,lastFrameAt=Date.now(),session=0;"
    "function connect(){if(paused)return;clearTimeout(retryTimer);retryTimer=0;"
    "state.textContent='CONNECTING';state.style.color='#e8c66a';"
    "feed.src='/stream?t='+Date.now()}"
    "function retry(){if(paused||retryTimer||retries>=8)return;"
    "const wait=Math.min(5000,500*Math.pow(2,retries++));"
    "state.textContent='RETRYING';state.style.color='#e8c66a';"
    "retryTimer=setTimeout(connect,wait)}"
    "feed.onerror=retry;"
    "document.getElementById('pause').onclick=()=>{paused=!paused;"
    "document.getElementById('pause').textContent=paused?'Resume':'Pause';"
    "if(paused){clearTimeout(retryTimer);retryTimer=0;feed.removeAttribute('src');"
    "state.textContent='PAUSED';state.style.color='#aeb7bc'}else{retries=0;connect()}};"
    "document.getElementById('reconnect').onclick=()=>{paused=false;retries=0;connect()};"
    "async function poll(){try{const response=await fetch('/status',{cache:'no-store'});"
    "if(!response.ok)throw Error();const s=await response.json();"
    "document.getElementById('robot').textContent=s.robot_id;"
    "document.getElementById('fps').textContent=Number(s.stream_fps).toFixed(1);"
    "document.getElementById('resolution').textContent=s.resolution;"
    "document.getElementById('rssi').textContent=s.rssi===null?'--':s.rssi;"
    "if(s.stream_session_id!==session){session=s.stream_session_id;lastFrames=s.frames_sent;"
    "lastFrameAt=Date.now()}else if(s.frames_sent!==lastFrames){lastFrames=s.frames_sent;"
    "lastFrameAt=Date.now();retries=0}"
    "if(!paused&&s.stream_session_active){state.textContent='ONLINE';state.style.color='#82d9a6';"
    "if(Date.now()-lastFrameAt>2500){feed.removeAttribute('src');retry()}}"
    "else if(!paused){state.textContent='CONNECTING';state.style.color='#e8c66a'}}"
    "catch(e){if(!paused){state.textContent='OFFLINE';state.style.color='#ef8d8d';retry()}}}"
    "setInterval(poll,1000);connect();poll();"
    "</script></body></html>";

static httpd_handle_t s_server;
static SemaphoreHandle_t s_client_gate;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static stream_server_snapshot_t s_state;
static stream_metrics_state_t s_metrics;
static uint32_t s_header_candidate;
static uint32_t s_header_candidate_count;
static uint32_t s_header_reference;
static bool s_header_reference_ready;

typedef enum {
    HEADER_FINGERPRINT_ACCEPTED,
    HEADER_FINGERPRINT_WARMUP,
    HEADER_FINGERPRINT_MISMATCH,
} header_fingerprint_result_t;

static const char *power_save_name(wifi_ps_type_t power_save)
{
    return power_save == WIFI_PS_NONE ? "NONE" : "UNEXPECTED";
}

stream_server_snapshot_t stream_server_get_snapshot(void)
{
    stream_server_snapshot_t snapshot;
    stream_metrics_state_t metrics;
    portENTER_CRITICAL(&s_state_mux);
    snapshot = s_state;
    metrics = s_metrics;
    portEXIT_CRITICAL(&s_state_mux);
    stream_metrics_get_snapshot(
        &metrics, esp_timer_get_time(), &snapshot.metrics);
    return snapshot;
}

static uint64_t claim_frame_id(void)
{
    uint64_t frame_id;
    portENTER_CRITICAL(&s_state_mux);
    frame_id = ++s_state.latest_frame_id;
    portEXIT_CRITICAL(&s_state_mux);
    return frame_id;
}

static void begin_stream_session(void)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state.stream_clients = 1U;
    stream_metrics_begin_session(&s_metrics, esp_timer_get_time());
    portEXIT_CRITICAL(&s_state_mux);
}

static void end_stream_session(void)
{
    portENTER_CRITICAL(&s_state_mux);
    stream_metrics_end_session(&s_metrics, esp_timer_get_time());
    s_state.stream_clients = 0U;
    portEXIT_CRITICAL(&s_state_mux);
}

static uint32_t duration_us(int64_t start_us, int64_t end_us)
{
    if (end_us <= start_us) {
        return 0U;
    }
    const uint64_t elapsed = (uint64_t)(end_us - start_us);
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static void record_camera_frame(uint32_t camera_wait_us, size_t jpeg_size)
{
    portENTER_CRITICAL(&s_state_mux);
    stream_metrics_record_camera_frame(
        &s_metrics, camera_wait_us, jpeg_size);
    portEXIT_CRITICAL(&s_state_mux);
}

static void record_camera_failure(uint32_t camera_wait_us)
{
    portENTER_CRITICAL(&s_state_mux);
    stream_metrics_record_camera_failure(&s_metrics, camera_wait_us);
    portEXIT_CRITICAL(&s_state_mux);
}

static void record_invalid_frame(uint32_t camera_wait_us)
{
    portENTER_CRITICAL(&s_state_mux);
    stream_metrics_record_invalid_frame(&s_metrics, camera_wait_us);
    portEXIT_CRITICAL(&s_state_mux);
}

static void record_header_result(header_fingerprint_result_t result)
{
    portENTER_CRITICAL(&s_state_mux);
    if (result == HEADER_FINGERPRINT_WARMUP) {
        stream_metrics_record_header_warmup(&s_metrics);
    } else if (result == HEADER_FINGERPRINT_MISMATCH) {
        stream_metrics_record_header_mismatch(&s_metrics);
    }
    portEXIT_CRITICAL(&s_state_mux);
}

static void record_send_result(esp_err_t result,
                               uint32_t send_us,
                               size_t jpeg_size)
{
    portENTER_CRITICAL(&s_state_mux);
    if (result == ESP_OK) {
        stream_metrics_record_send_success(&s_metrics, send_us, jpeg_size);
    } else {
        stream_metrics_record_send_failure(&s_metrics, send_us, result);
    }
    portEXIT_CRITICAL(&s_state_mux);
}

static header_fingerprint_result_t check_header_fingerprint(uint32_t fingerprint)
{
    if (s_header_reference_ready) {
        if (fingerprint != s_header_reference) {
            ESP_LOGW(TAG,
                     "header fingerprint mismatch: expected=%08" PRIx32
                     " actual=%08" PRIx32,
                     s_header_reference,
                     fingerprint);
            return HEADER_FINGERPRINT_MISMATCH;
        }
        return HEADER_FINGERPRINT_ACCEPTED;
    }

    if (s_header_candidate_count == 0U ||
        fingerprint != s_header_candidate) {
        s_header_candidate = fingerprint;
        s_header_candidate_count = 1U;
        return HEADER_FINGERPRINT_WARMUP;
    }

    ++s_header_candidate_count;
    if (s_header_candidate_count < STREAM_HEADER_CONSENSUS_FRAMES) {
        return HEADER_FINGERPRINT_WARMUP;
    }

    s_header_reference = fingerprint;
    s_header_reference_ready = true;
    ESP_LOGI(TAG,
             "JPEG header reference locked: fingerprint=%08" PRIx32,
             s_header_reference);
    return HEADER_FINGERPRINT_ACCEPTED;
}

static esp_err_t send_status_json(httpd_req_t *request)
{
    const wifi_manager_snapshot_t wifi = wifi_manager_get_snapshot();
    const stream_server_snapshot_t stream = stream_server_get_snapshot();
    const stream_metrics_snapshot_t *metrics = &stream.metrics;
    const unsigned internal_heap_free = (unsigned)heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned internal_heap_min =
        (unsigned)heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned psram_free = (unsigned)heap_caps_get_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const unsigned psram_min = (unsigned)heap_caps_get_minimum_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char ip_address[IP4ADDR_STRLEN_MAX];
    snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&wifi.ip));

    cJSON *status = cJSON_CreateObject();
    if (status == NULL) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "status allocation failed");
    }

    cJSON_AddStringToObject(status, "robot_id", CONFIG_ROBOT_ID);
    cJSON_AddNumberToObject(
        status, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddBoolToObject(status, "wifi_connected", wifi.connected);
    cJSON_AddStringToObject(status, "ip", wifi.connected ? ip_address : "0.0.0.0");
    if (wifi.rssi_valid) {
        cJSON_AddNumberToObject(status, "rssi", wifi.rssi);
    } else {
        cJSON_AddNullToObject(status, "rssi");
    }
    cJSON_AddNumberToObject(status, "wifi_reconnects", wifi.reconnect_count);
    cJSON_AddStringToObject(status, "power_save", power_save_name(wifi.power_save));
    cJSON_AddStringToObject(status, "resolution", "320x240");
    cJSON_AddStringToObject(status, "pixel_format", "JPEG");
    cJSON_AddNumberToObject(status, "jpeg_quality", 20);
    cJSON_AddNumberToObject(status, "xclk_mhz", 20);
    cJSON_AddNumberToObject(status, "fb_count", 2);
    cJSON_AddStringToObject(status, "grab_mode", "LATEST");
    cJSON_AddBoolToObject(status, "psram_dma", false);
    cJSON_AddNumberToObject(status, "stream_clients", stream.stream_clients);
    cJSON_AddNumberToObject(status, "d1_camera_fps", D2_CAMERA_REFERENCE_FPS);
    cJSON_AddNumberToObject(
        status, "stream_session_id", (double)metrics->session_id);
    cJSON_AddBoolToObject(
        status, "stream_session_active", metrics->session_active);
    cJSON_AddNumberToObject(
        status, "stream_elapsed_s", metrics->elapsed_seconds);
    cJSON_AddNumberToObject(
        status, "camera_frames", (double)metrics->camera_frames);
    cJSON_AddNumberToObject(
        status, "camera_failures", (double)metrics->camera_failures);
    cJSON_AddNumberToObject(
        status, "invalid_frames", (double)metrics->invalid_frames);
    cJSON_AddNumberToObject(status,
                            "header_warmup_frames",
                            (double)metrics->header_warmup_frames);
    cJSON_AddNumberToObject(status,
                            "header_mismatch_frames",
                            (double)metrics->header_mismatch_frames);
    cJSON_AddNumberToObject(status, "camera_fps", metrics->camera_fps);
    cJSON_AddNumberToObject(
        status, "avg_camera_wait_ms", metrics->average_camera_wait_ms);
    cJSON_AddNumberToObject(
        status, "frames_sent", (double)metrics->frames_sent);
    cJSON_AddNumberToObject(status, "stream_fps", metrics->stream_fps);
    cJSON_AddNumberToObject(
        status, "bytes_sent", (double)metrics->bytes_sent);
    cJSON_AddNumberToObject(
        status, "throughput_bps", metrics->throughput_bps);
    cJSON_AddNumberToObject(
        status, "avg_jpeg_size", metrics->average_jpeg_size);
    cJSON_AddNumberToObject(
        status, "avg_send_ms", metrics->average_send_ms);
    cJSON_AddNumberToObject(status, "p95_send_ms", metrics->p95_send_ms);
    cJSON_AddNumberToObject(
        status, "send_failures", (double)metrics->send_failures);
    if (metrics->last_send_error_valid) {
        cJSON_AddStringToObject(
            status, "last_send_error", esp_err_to_name(metrics->last_send_error));
    } else {
        cJSON_AddNullToObject(status, "last_send_error");
    }
    cJSON_AddNumberToObject(status, "internal_heap_free", internal_heap_free);
    cJSON_AddNumberToObject(status, "internal_heap_min", internal_heap_min);
    cJSON_AddNumberToObject(status, "psram_free", psram_free);
    cJSON_AddNumberToObject(status, "psram_min", psram_min);

    char *json = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    if (json == NULL) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "status serialization failed");
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_send(request, json, strlen(json));
    cJSON_free(json);
    return result;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    return send_status_json(request);
}

static esp_err_t viewer_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, VIEWER_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_session(httpd_req_t *request)
{
    const size_t packet_capacity =
        (size_t)CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE + STREAM_PACKET_OVERHEAD;
    uint8_t *packet = heap_caps_malloc(
        packet_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (packet == NULL) {
        ESP_LOGE(TAG, "unable to allocate stream staging buffer");
        return ESP_ERR_NO_MEM;
    }

    const int socket_fd = httpd_req_to_sockfd(request);
    const int no_delay = 1;
    if (setsockopt(socket_fd,
                   IPPROTO_TCP,
                   TCP_NODELAY,
                   &no_delay,
                   sizeof(no_delay)) < 0) {
        ESP_LOGE(TAG, "unable to enable TCP_NODELAY for stream client");
        heap_caps_free(packet);
        return ESP_FAIL;
    }

    esp_err_t result = httpd_resp_set_type(request, MJPEG_STREAM_CONTENT_TYPE);
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request, "Cache-Control", "no-cache, no-store, must-revalidate");
    }
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(request, "Pragma", "no-cache");
    }

    while (result == ESP_OK) {
        const int64_t camera_wait_start_us = esp_timer_get_time();
        camera_fb_t *frame = esp_camera_fb_get();
        const int64_t capture_timestamp_us = esp_timer_get_time();
        const uint32_t camera_wait_us = duration_us(
            camera_wait_start_us, capture_timestamp_us);
        if (frame == NULL) {
            record_camera_failure(camera_wait_us);
            ESP_LOGW(TAG,
                     "Camera framebuffer unavailable: camera_wait_us=%" PRIu32,
                     camera_wait_us);
            continue;
        }

        uint32_t header_fingerprint = 0U;
        if (!camera_frame_is_valid(frame) ||
            !jpeg_data_get_header_fingerprint(
                frame->buf, frame->len, &header_fingerprint)) {
            record_invalid_frame(camera_wait_us);
            ESP_LOGW(TAG,
                     "invalid Camera framebuffer skipped: jpeg_size=%u "
                     "camera_wait_us=%" PRIu32,
                     (unsigned)frame->len,
                     camera_wait_us);
            esp_camera_fb_return(frame);
            continue;
        }

        record_camera_frame(camera_wait_us, frame->len);
        const header_fingerprint_result_t header_result =
            check_header_fingerprint(header_fingerprint);
        if (header_result != HEADER_FINGERPRINT_ACCEPTED) {
            record_header_result(header_result);
            esp_camera_fb_return(frame);
            continue;
        }

        const uint64_t frame_id = claim_frame_id();
        const size_t jpeg_size = frame->len;
        char part_header[256];
        const int header_length = mjpeg_stream_format_part_header(
            part_header,
            sizeof(part_header),
            jpeg_size,
            frame_id,
            capture_timestamp_us);
        const size_t packet_size = header_length > 0
            ? (size_t)header_length + jpeg_size + 2U
            : 0U;
        if (header_length < 0 || packet_size > packet_capacity) {
            result = ESP_ERR_INVALID_SIZE;
        } else {
            memcpy(packet, part_header, (size_t)header_length);
            memcpy(packet + header_length, frame->buf, jpeg_size);
            memcpy(packet + header_length + jpeg_size, "\r\n", 2U);
        }

        esp_camera_fb_return(frame);
        frame = NULL;

        const int64_t send_start_us = esp_timer_get_time();
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(
                request, (const char *)packet, packet_size);
        }

        const uint32_t send_us = duration_us(
            send_start_us, esp_timer_get_time());
        record_send_result(result, send_us, jpeg_size);
        if (result != ESP_OK) {
            ESP_LOGW(TAG,
                     "frame send failed: frame_id=%" PRIu64
                     " jpeg_size=%u camera_wait_us=%" PRIu32
                     " send_us=%" PRIu32 " result=%s",
                     frame_id,
                     (unsigned)jpeg_size,
                     camera_wait_us,
                     send_us,
                     esp_err_to_name(result));
        }
    }
    heap_caps_free(packet);
    return result;
}

static void stream_worker(void *argument)
{
    httpd_req_t *request = argument;
    ESP_LOGI(TAG, "stream client connected");
    const esp_err_t stream_result = stream_session(request);
    const esp_err_t complete_result = httpd_req_async_handler_complete(request);

    end_stream_session();
    xSemaphoreGive(s_client_gate);
    ESP_LOGI(TAG,
             "stream client released: send=%s async_complete=%s",
             esp_err_to_name(stream_result),
             esp_err_to_name(complete_result));
    vTaskDelete(NULL);
}

static esp_err_t send_busy_response(httpd_req_t *request)
{
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Retry-After", "1");
    return httpd_resp_sendstr(request, "{\"error\":\"stream client already active\"}");
}

static esp_err_t stream_handler(httpd_req_t *request)
{
    if (xSemaphoreTake(s_client_gate, 0) != pdTRUE) {
        return send_busy_response(request);
    }

    httpd_req_t *async_request = NULL;
    esp_err_t result = httpd_req_async_handler_begin(request, &async_request);
    if (result != ESP_OK) {
        xSemaphoreGive(s_client_gate);
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "async stream unavailable");
    }

    begin_stream_session();
    if (xTaskCreate(stream_worker,
                    "mjpeg_stream",
                    STREAM_TASK_STACK_SIZE,
                    async_request,
                    STREAM_TASK_PRIORITY,
                    NULL) != pdPASS) {
        end_stream_session();
        xSemaphoreGive(s_client_gate);
        httpd_resp_send_err(
            async_request, HTTPD_500_INTERNAL_SERVER_ERROR, "stream task unavailable");
        httpd_req_async_handler_complete(async_request);
    }
    return ESP_OK;
}

esp_err_t stream_server_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_client_gate = xSemaphoreCreateBinary();
    if (s_client_gate == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_client_gate);
    stream_metrics_init(&s_metrics);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 3U;
    config.lru_purge_enable = false;
    config.send_wait_timeout = 2U;

    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        vSemaphoreDelete(s_client_gate);
        s_client_gate = NULL;
        return result;
    }

    const httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
    };
    const httpd_uri_t viewer_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = viewer_handler,
    };
    const httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };
    if ((result = httpd_register_uri_handler(s_server, &viewer_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(s_server, &status_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(s_server, &stream_uri)) != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        vSemaphoreDelete(s_client_gate);
        s_client_gate = NULL;
        return result;
    }

    ESP_LOGI(TAG, "HTTP server started: GET /, GET /status, GET /stream");
    return ESP_OK;
}

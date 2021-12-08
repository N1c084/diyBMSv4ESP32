
#include "webserver.h"

#include <stdio.h>

httpd_handle_t _myserver;
String UUIDString;
String UUIDStringLast2Chars;

// Pointers to other classes (not always a good idea in static classes)
// static sdcard_info (*_sdcardcallback)();
fs::SDFS *_sdcard;
void (*_sdcardaction_callback)(uint8_t action);
PacketRequestGenerator *_prg;
PacketReceiveProcessor *_receiveProc;
diybms_eeprom_settings *_mysettings;
Rules *_rules;
ControllerState *_controlState;
HAL_ESP32 *_hal;

void generateUUID()
{
  uint8_t uuidNumber[16]; // UUIDs in binary form are 16 bytes long

  // ESP32 has inbuilt random number generator
  // https://techtutorialsx.com/2017/12/22/esp32-arduino-random-number-generation/
  for (uint8_t x = 0; x < 16; x++)
  {
    uuidNumber[x] = random(0xFF);
  }

  UUIDString = uuidToString(uuidNumber);

  // 481efb3f-0400-0000-101f-fb3fd01efb3f
  UUIDStringLast2Chars = UUIDString.substring(34);
}

String uuidToString(uint8_t *uuidLocation)
{
  const char hexchars[] = "0123456789abcdef";
  String string = "";
  int i;
  for (i = 0; i < 16; i++)
  {
    if (i == 4)
      string += "-";
    if (i == 6)
      string += "-";
    if (i == 8)
      string += "-";
    if (i == 10)
      string += "-";
    uint8_t topDigit = uuidLocation[i] >> 4;
    uint8_t bottomDigit = uuidLocation[i] & 0x0f;
    // High hex digit
    string += hexchars[topDigit];
    // Low hex digit
    string += hexchars[bottomDigit];
  }

  return string;
}

void setCacheControl(httpd_req_t *req)
{
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

String TemplateProcessor(const String &var)
{
  if (var == "XSS_KEY")
    return UUIDString;

#if defined(ESP32)
  if (var == "PLATFORM")
    return String("ESP32");
#endif

  if (var == "LANGUAGE")
    return String(_mysettings->language);

  if (var == "GIT_VERSION")
    return String(GIT_VERSION);

  if (var == "COMPILE_DATE_TIME")
    return String(COMPILE_DATE_TIME);

  if (var == "graph_voltagehigh")
    return String(_mysettings->graph_voltagehigh);

  if (var == "graph_voltagelow")
    return String(_mysettings->graph_voltagelow);

  if (var == "integrity_file_jquery_js")
    return String(integrity_file_jquery_js);

  if (var == "noofseriesmodules")
    return String(maximum_controller_cell_modules);

  if (var == "maxnumberofbanks")
    return String(maximum_number_of_banks);

  return String();
}

esp_err_t content_handler_monitor2(httpd_req_t *req)
{
  httpd_resp_set_type(req, "application/json");
  setCacheControl(req);

  uint8_t totalModules = _mysettings->totalNumberOfBanks * _mysettings->totalNumberOfSeriesModules;

  // const char comma = ',';
  // const char *null = "null";
  //  AsyncResponseStream *response = request->beginResponseStream("application/json");

#define BUFSIZE 1024

  char buf[BUFSIZE];
  int bufferused = 0;
  const char *nullstring = "null";

  // Output the first batch of settings/parameters/values
  bufferused += snprintf(&buf[bufferused], BUFSIZE,
                         "{\"banks\":%u,\"seriesmodules\":%u,\"sent\":%u,\"received\":%u,\"modulesfnd\":%u,\"badcrc\":%u,\"ignored\":%u,\"roundtrip\":%u,\"oos\":%u,\"activerules\":%u,\"uptime\":%u,\"can_fail\":%u,\"can_sent\":%u,\"can_rec\":%u,\"sec\":\"%s\",",
                         _mysettings->totalNumberOfBanks, _mysettings->totalNumberOfSeriesModules,
                         _prg->packetsGenerated, _receiveProc->packetsReceived,
                         _receiveProc->totalModulesFound, _receiveProc->totalCRCErrors,
                         _receiveProc->totalNotProcessedErrors,
                         _receiveProc->packetTimerMillisecond, _receiveProc->totalOutofSequenceErrors, _rules->active_rule_count, (uint32_t)(esp_timer_get_time() / (uint64_t)1e+6), canbus_messages_failed_sent, canbus_messages_sent, canbus_messages_received, UUIDStringLast2Chars.c_str());

  // current
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"current\":[");

  if (_mysettings->currentMonitoringEnabled && currentMonitor.validReadings)
  {

    // Output current monitor values, this is inside an array, so could be more than 1
    bufferused += snprintf(&buf[bufferused], BUFSIZE,
                           "{\"c\":%.4f,\"v\":%.4f,\"mahout\":%u,\"mahin\":%u,\"p\":%.2f,\"soc\":%.2f}",
                           currentMonitor.modbus.current, currentMonitor.modbus.voltage, currentMonitor.modbus.milliamphour_out,
                           currentMonitor.modbus.milliamphour_in, currentMonitor.modbus.power, currentMonitor.stateofcharge);
  }
  else
  {
    bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
  }

  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  bufferused += snprintf(&buf[bufferused], BUFSIZE, "\"errors\":[");
  uint8_t count = 0;

  for (size_t i = 0; i < sizeof(_rules->ErrorCodes); i++)
  {
    if (_rules->ErrorCodes[i] != InternalErrorCode::NoError)
    {
      // Comma if not zero
      if (count)
      {
        bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");
      }
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", _rules->ErrorCodes[i]);
      count++;
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],\"warnings\":[");

  count = 0;
  for (size_t i = 0; i < sizeof(_rules->WarningCodes); i++)
  {
    if (_rules->WarningCodes[i] != InternalWarningCode::NoWarning)
    {
      // Comma if not zero
      if (count)
      {
        bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");
      }

      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", _rules->WarningCodes[i]);
      count++;
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);

  // Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // voltages
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"voltages\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");
    }

    if (cmi[i].valid)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", cmi[i].voltagemV);
    }
    else
    {
      // Module is not yet valid so return null values...
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);

  // Send it...
  httpd_resp_sendstr_chunk(req, buf);

  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"minvoltages\":[");
  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", cmi[i].voltagemVMin);
    }
    else
    {
      // Module is not yet valid so return null values...
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // maxvoltages
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"maxvoltages\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", cmi[i].voltagemVMin);
    }
    else
    {
      // Module is not yet valid so return null values...
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // inttemp
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"inttemp\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid && cmi[i].internalTemp != -40)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%i", cmi[i].internalTemp);
    }
    else
    {
      // Module is not yet valid so return null values...
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // exttemp
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"exttemp\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid && cmi[i].externalTemp != -40)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%i", cmi[i].externalTemp);
    }
    else
    {
      // Module is not yet valid so return null values...
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%s", nullstring);
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // bypass
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"bypass\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid && cmi[i].inBypass)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "1");
    }
    else
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "0");
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // bypasshot
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"bypasshot\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid && cmi[i].bypassOverTemp)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "1");
    }
    else
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "0");
    }
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // bypasspwm
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"bypasspwm\":[");

  for (uint8_t i = 0; i < totalModules; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    if (cmi[i].valid && cmi[i].inBypass)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", cmi[i].PWMValue);
    }
    else
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "0");
    }
  }

  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // bankv
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"bankv\":[");

  for (uint8_t i = 0; i < _mysettings->totalNumberOfBanks; i++)
  {
    // Comma if not zero
    if (i)
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");

    bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", _rules->packvoltage[i]);
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "],");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // voltrange
  bufferused = 0;
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "\"voltrange\":[");

  for (uint8_t i = 0; i < _mysettings->totalNumberOfBanks; i++)
  {
    // Comma if not zero
    if (i)
    {
      bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, ",");
    }

    bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "%u", _rules->VoltageRangeInBank(i));
  }
  bufferused += snprintf(&buf[bufferused], BUFSIZE - bufferused, "]}");

  // ESP_LOGD(TAG, "bufferused=%i", bufferused);  ESP_LOGD(TAG, "monitor2: %s", buf);
  //  Send it...
  httpd_resp_sendstr_chunk(req, buf);

  // Indicate last chunk (zero byte length)
  return httpd_resp_send_chunk(req, buf, 0);
}

// The main home page
esp_err_t default_htm_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  setCacheControl(req);

  char *file_pointer = (char *)file_default_htm;
  size_t max_len = size_file_default_htm;
  char *end_pointer = file_pointer + max_len;

  // Seek out the template strings to inline replace
  // this is likely to crash with poorly formatted HTML input
  ESP_LOGI(TAG, "default.htm");

  char *p = file_pointer;

  while (p < end_pointer)
  {
    const char *templateCharacter = "%";

    // Find the next escape character (start of a template keyword)
    char *start_ptr = (char *)memchr(p, '%', end_pointer - p);

    if (start_ptr != NULL)
    {
      // Output to client up to the start of the template string
      httpd_resp_send_chunk(req, p, start_ptr - p);

      // Skip over template character %
      p = start_ptr + 1;

      // Find the end of the template
      char *end_ptr = (char *)memchr(p, '%', end_pointer - p);

      if (end_ptr != NULL)
      {
        // Length of the template keyword
        int paramNameLength = end_ptr - p;

        if (paramNameLength > 0)
        {
          char buf[50 + 1];
          memcpy(buf, p, paramNameLength);
          buf[paramNameLength] = 0;
          String paramName = String(buf);
          String templateValue = TemplateProcessor(paramName);

          ESP_LOGD(TAG, "Template %s = '%s'", paramName.c_str(), templateValue.c_str());

          // Output the template answer
          httpd_resp_sendstr_chunk(req, templateValue.c_str());
        }
        else
        {
          // Its an empty keyword %% - so just output a % character
          httpd_resp_sendstr_chunk(req, templateCharacter);
        }

        end_ptr++;
        p = end_ptr;
      }
      else
      {
        break;
      }
    }
    else
    {
      break;
    }
  }

  // output the last chunk
  if (p < end_pointer)
  {
    httpd_resp_send_chunk(req, p, end_pointer - p);
  }

  // Indicate last chunk (zero byte length)
  return httpd_resp_send_chunk(req, p, 0);
}

void SetCacheAndETag(httpd_req_t *req, const char *ETag)
{
  httpd_resp_set_hdr(req, "ETag", ETag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, max-age=86400");
}

typedef struct
{
  const uint8_t *resp;
  size_t resp_len;
  const char *etag;
  const char *mimetype;
} WEBKIT_RESPONSE_ARGS;

const char *const text_css = "text/css";
const char *const application_javascript = "application/javascript";
const char *const image_png = "image/png";
const char *const image_x_icon = "image/x-icon";

// Handle static files which are NOT GZIP compressed
esp_err_t static_content_handler(httpd_req_t *req)
{
  WEBKIT_RESPONSE_ARGS *args = (WEBKIT_RESPONSE_ARGS *)(req->user_ctx);

  httpd_resp_set_type(req, args->mimetype);

  // TODO: GET ETAGS WORKING AGAIN!!
  char buffer[50];

  if (httpd_req_get_hdr_value_str(req, "If-None-Match", buffer, sizeof(buffer)) == ESP_OK)
  {
    // We have a value in the eTag header

    if (strncmp(buffer, args->etag, strlen(args->etag)) == 0)
    {
      ESP_LOGD(TAG, "Cached response for %s", req->uri);
      // Matched
      httpd_resp_set_status(req, "304 Not Modified");
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }
  }

  ESP_LOGD(TAG, "Web serve %s", req->uri);
  SetCacheAndETag(req, args->etag);
  return httpd_resp_send(req, (const char *)args->resp, args->resp_len);
}

// Handle static files which are already GZIP compressed
esp_err_t static_content_handler_gzipped(httpd_req_t *req)
{
  WEBKIT_RESPONSE_ARGS *args = (WEBKIT_RESPONSE_ARGS *)(req->user_ctx);

  httpd_resp_set_type(req, args->mimetype);

  // TODO: GET ETAGS WORKING AGAIN!!
  char buffer[50];

  if (httpd_req_get_hdr_value_str(req, "If-None-Match", buffer, sizeof(buffer)) == ESP_OK)
  {
    // We have a value in the eTag header

    if (strncmp(buffer, args->etag, strlen(args->etag)) == 0)
    {
      ESP_LOGD(TAG, "Cached response for %s", req->uri);
      // Matched
      httpd_resp_set_status(req, "304 Not Modified");
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }
  }

  ESP_LOGD(TAG, "Web serve %s", req->uri);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  SetCacheAndETag(req, args->etag);
  return httpd_resp_send(req, (const char *)args->resp, args->resp_len);
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t get_root_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_status(req, "301 Moved Permanently");
  httpd_resp_set_hdr(req, "Location", "/default.htm");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
  /* Destination buffer for content of HTTP POST request.
   * httpd_req_recv() accepts char* only, but content could
   * as well be any binary data (needs type casting).
   * In case of string data, null termination will be absent, and
   * content length would give length of string */
  char content[100];

  /* Truncate if content length larger than the buffer */
  size_t recv_size = min(req->content_len, sizeof(content));

  int ret = httpd_req_recv(req, content, recv_size);
  if (ret <= 0)
  { /* 0 return value indicates connection closed */
    /* Check if timeout occurred */
    if (ret == HTTPD_SOCK_ERR_TIMEOUT)
    {
      /* In case of timeout one can choose to retry calling
       * httpd_req_recv(), but to keep it simple, here we
       * respond with an HTTP 408 (Request Timeout) error */
      httpd_resp_send_408(req);
    }
    /* In case of error, returning ESP_FAIL will
     * ensure that the underlying socket is closed */
    return ESP_FAIL;
  }

  /* Send a simple response */
  const char resp[] = "URI POST Response";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_root_get = {.uri = "/", .method = HTTP_GET, .handler = get_root_handler, .user_ctx = NULL};

httpd_uri_t uri_defaulthtm_get = {.uri = "/default.htm", .method = HTTP_GET, .handler = default_htm_handler, .user_ctx = NULL};

WEBKIT_RESPONSE_ARGS webkit_style_css_args = {file_style_css_gz, size_file_style_css_gz, etag_file_style_css_gz, text_css};
httpd_uri_t uri_stylecss_get = {.uri = "/style.css", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_style_css_args};

WEBKIT_RESPONSE_ARGS webkit_pagecode_js_args = {file_pagecode_js_gz, size_file_pagecode_js_gz, etag_file_pagecode_js_gz, application_javascript};
httpd_uri_t uri_pagecode_js_get = {.uri = "/pagecode.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_pagecode_js_args};

WEBKIT_RESPONSE_ARGS webkit_jquery_js_args = {file_jquery_js_gz, size_file_jquery_js_gz, etag_file_jquery_js_gz, application_javascript};
httpd_uri_t uri_jquery_js_get = {.uri = "/jquery.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_jquery_js_args};

WEBKIT_RESPONSE_ARGS webkit_notify_min_js_args = {file_notify_min_js_gz, size_file_notify_min_js_gz, etag_file_notify_min_js_gz, application_javascript};
httpd_uri_t uri_notify_min_js_get = {.uri = "/notify.min.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_notify_min_js_args};

WEBKIT_RESPONSE_ARGS webkit_echarts_min_js_args = {file_echarts_min_js_gz, size_file_echarts_min_js_gz, etag_file_echarts_min_js_gz, application_javascript};
httpd_uri_t uri_echarts_min_js_get = {.uri = "/echarts.min.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_echarts_min_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_ru_js_args = {file_lang_ru_js_gz, size_file_lang_ru_js_gz, etag_file_lang_ru_js_gz, application_javascript};
httpd_uri_t uri_lang_ru_js_get = {.uri = "/lang_ru.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_ru_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_hr_js_args = {file_lang_hr_js_gz, size_file_lang_hr_js_gz, etag_file_lang_hr_js_gz, application_javascript};
httpd_uri_t uri_lang_hr_js_get = {.uri = "/lang_hr.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_hr_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_nl_js_args = {file_lang_nl_js_gz, size_file_lang_nl_js_gz, etag_file_lang_nl_js_gz, application_javascript};
httpd_uri_t uri_lang_nl_js_get = {.uri = "/lang_nl.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_nl_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_pt_js_args = {file_lang_pt_js_gz, size_file_lang_pt_js_gz, etag_file_lang_pt_js_gz, application_javascript};
httpd_uri_t uri_lang_pt_js_get = {.uri = "/lang_pt.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_pt_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_de_js_args = {file_lang_de_js_gz, size_file_lang_de_js_gz, etag_file_lang_de_js_gz, application_javascript};
httpd_uri_t uri_lang_de_js_get = {.uri = "/lang_de.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_de_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_es_js_args = {file_lang_es_js_gz, size_file_lang_es_js_gz, etag_file_lang_es_js_gz, application_javascript};
httpd_uri_t uri_lang_es_js_get = {.uri = "/lang_es.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_es_js_args};

WEBKIT_RESPONSE_ARGS webkit_lang_en_js_args = {file_lang_en_js_gz, size_file_lang_en_js_gz, etag_file_lang_en_js_gz, application_javascript};
httpd_uri_t uri_lang_en_js_get = {.uri = "/lang_en.js", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_lang_en_js_args};

WEBKIT_RESPONSE_ARGS webkit_favicon_ico_args = {file_favicon_ico_gz, size_file_favicon_ico_gz, etag_file_favicon_ico_gz, image_x_icon};
httpd_uri_t uri_favicon_ico_get = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = static_content_handler_gzipped, .user_ctx = (void *)&webkit_favicon_ico_args};

WEBKIT_RESPONSE_ARGS webkit_logo_png_args = {file_logo_png, size_file_logo_png, etag_file_logo_png, image_png};
httpd_uri_t uri_logo_png_get = {.uri = "/logo.png", .method = HTTP_GET, .handler = static_content_handler, .user_ctx = (void *)&webkit_logo_png_args};

WEBKIT_RESPONSE_ARGS webkit_wait_png_args = {file_wait_png, size_file_wait_png, etag_file_wait_png, image_png};
httpd_uri_t uri_wait_png_get = {.uri = "/wait.png", .method = HTTP_GET, .handler = static_content_handler, .user_ctx = (void *)&webkit_wait_png_args};

WEBKIT_RESPONSE_ARGS webkit_patron_png_args = {file_patron_png, size_file_patron_png, etag_file_patron_png, image_png};
httpd_uri_t uri_patron_png_get = {.uri = "/patron.png", .method = HTTP_GET, .handler = static_content_handler, .user_ctx = (void *)&webkit_patron_png_args};

WEBKIT_RESPONSE_ARGS webkit_warning_png_args = {file_warning_png, size_file_warning_png, etag_file_warning_png, image_png};
httpd_uri_t uri_warning_png_get = {.uri = "/warning.png", .method = HTTP_GET, .handler = static_content_handler, .user_ctx = (void *)&webkit_warning_png_args};

httpd_uri_t uri_monitor2_json_get = {.uri = "/monitor2.json", .method = HTTP_GET, .handler = content_handler_monitor2, .user_ctx = NULL};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {.uri = "/uri", .method = HTTP_POST, .handler = post_handler, .user_ctx = NULL};

void clearModuleValues(uint8_t module)
{
  cmi[module].valid = false;
  cmi[module].voltagemV = 0;
  cmi[module].voltagemVMin = 6000;
  cmi[module].voltagemVMax = 0;
  cmi[module].badPacketCount = 0;
  cmi[module].inBypass = false;
  cmi[module].bypassOverTemp = false;
  cmi[module].internalTemp = -40;
  cmi[module].externalTemp = -40;
}

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
  /* Generate default configuration */
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.max_uri_handlers = 32;

  /* Empty handle to esp_http_server */
  httpd_handle_t server = NULL;

  /* Start the httpd server */
  if (httpd_start(&server, &config) == ESP_OK)
  {

    /* Register URI handlers */
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_root_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_defaulthtm_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_stylecss_get));
    // Images
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_favicon_ico_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_warning_png_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_patron_png_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_logo_png_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_wait_png_get));

    // Javascript libs...
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_pagecode_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_jquery_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_notify_min_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_echarts_min_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_ru_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_hr_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_nl_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_pt_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_de_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_es_js_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_lang_en_js_get));

    // Web services/API
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_monitor2_json_get));

    // httpd_register_uri_handler(server, &uri_post);
  }
  /* If server failed to start, handle will be NULL */
  return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
  if (server)
  {
    /* Stop the httpd server */
    httpd_stop(server);
  }
}

// Start Web Server (crazy amount of pointer params!)
void StartServer(diybms_eeprom_settings *mysettings,
                 fs::SDFS *sdcard,
                 PacketRequestGenerator *prg,
                 PacketReceiveProcessor *pktreceiveproc,
                 ControllerState *controlState,
                 Rules *rules,
                 void (*sdcardaction_callback)(uint8_t action),
                 HAL_ESP32 *hal)
{
  _myserver = start_webserver();
  _hal = hal;
  _prg = prg;
  _controlState = controlState;
  _rules = rules;
  _sdcard = sdcard;
  _mysettings = mysettings;
  _receiveProc = pktreceiveproc;
  _sdcardaction_callback = sdcardaction_callback;
}

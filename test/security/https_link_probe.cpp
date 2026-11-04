// Compile/link investigation only. No certificate or authentication is set up.
// Do not flash or deploy: this is not the clock or a working HTTPS service.
#include <Arduino.h>
#include <esp_https_server.h>

void httpsLinkProbe() {
  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  httpd_handle_t handle = nullptr;
  const esp_err_t result = httpd_ssl_start(&handle, &config);
  if (result == ESP_OK) httpd_ssl_stop(handle);
}

#ifndef WALLCLOCK_HTTPS_LINK_WITH_CLOCK
void setup() { httpsLinkProbe(); }
void loop() { delay(1000); }
#endif

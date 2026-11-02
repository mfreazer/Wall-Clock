// Host-only WebServer shim. NOT the real ESP32 WebServer.
//
// Routes are registered exactly as the firmware registers them; the harness
// dispatches a request to the matching handler and inspects the captured
// response. Body/header access follows the ESP32 semantics the firmware
// relies on: the POST body is available as arg("plain") and arguments are
// empty otherwise.
#pragma once

#include <Arduino.h>

#include <functional>

#define HTTP_GET 1
#define HTTP_POST 3

struct HostHttpRequest {
  std::string path;
  int method = HTTP_GET;
  std::string body;
  std::map<std::string, std::string> headers;

  std::string header(const std::string& name) const {
    auto it = headers.find(name);
    return it == headers.end() ? std::string() : it->second;
  }
};

struct HostHttpResponse {
  int code = 0;
  std::string contentType;
  std::string body;
  bool sent = false;
  std::map<std::string, std::string> headers;
};

class WebServer {
 public:
  struct Route {
    std::string path;
    int method;
    std::function<void()> handler;
  };

  std::vector<Route> routes;
  std::vector<std::string> collectedHeaders;
  bool started = false;

  HostHttpRequest req;
  HostHttpResponse res;
  std::function<void()> notFound;

  explicit WebServer(int port) : port_(port) {}
  int port() const { return port_; }

  void on(const char* path, int method, std::function<void()> handler) { routes.push_back({path, method, handler}); }
  void onNotFound(std::function<void()> handler) { notFound = handler; }

  void collectHeaders(const char* names[], int count) {
    collectedHeaders.assign(names, names + count);
  }

  void begin() { started = true; }
  void handleClient() {}

  String arg(const char* name) const {
    return (name && std::string(name) == "plain") ? String(req.body) : String();
  }
  String arg(const String& name) const { return arg(name.c_str()); }
  bool hasArg(const char* name) const { return name && std::string(name) == "plain" && !req.body.empty(); }

  String header(const char* name) const { return String(req.header(name ? name : "")); }
  bool hasHeader(String name) const { return !req.header(name.s).empty(); }
  void sendHeader(const String& name, const String& value, bool = false) { res.headers[name.s]=value.s; }
  size_t clientContentLength() const { return req.body.size(); }

  void send(int code, const char* contentType, const String& body) { res.code=code; res.contentType=contentType ? contentType : ""; res.body=body.s; res.sent=true; }
  void send_P(int code, const char* contentType, const char* body) { res.code=code; res.contentType=contentType ? contentType : ""; res.body=body ? body : ""; res.sent=true; }
  void send(int code) { res.code=code; res.sent=true; }

 private:
  int port_;
};

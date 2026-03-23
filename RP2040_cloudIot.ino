// RP2040 WebSocket LED dashboard (AP + Web UI).
// Configure ssid/password in defines.h; open Serial Monitor for AP IP.
#include "defines.h"
#include <WebSockets2_Generic.h>
#include <WiFiWebServer.h>
#include <vector>

using namespace websockets2_generic;

WebsocketsServer SocketsServer;
#define WEBSOCKETS_PORT 8080
struct ClientSlot {
  WebsocketsClient* client;
  unsigned long lastActiveMs;
  bool disconnected;
};
std::vector<ClientSlot> clients;

WiFiWebServer server(80);
bool ledState = false; // Track LED state

enum WebSocketEventType {
  WS_CONNECTED,
  WS_DISCONNECTED,
  WS_MESSAGE
};

static void touchClient(WebsocketsClient* c) {
  for (size_t i = 0; i < clients.size(); ++i) {
    if (clients[i].client == c) {
      clients[i].lastActiveMs = millis();
      break;
    }
  }
}

static void markDisconnected(WebsocketsClient* c) {
  for (size_t i = 0; i < clients.size(); ++i) {
    if (clients[i].client == c) {
      clients[i].disconnected = true;
      break;
    }
  }
}

void broadcastMessage(const String &message) {
  // send to all clients; remove dead ones while iterating
  for (int i = (int)clients.size() - 1; i >= 0; --i) {
    WebsocketsClient* c = clients[i].client;
    if (!c){
      clients.erase(clients.begin()+i);
      continue;
    }
    c->send(message);
  }
  Serial.println(millis());
  Serial.println("Broadcast: " + message);
}

void handleWebSocketEvent(WebSocketEventType type, const String &payload, WebsocketsClient* origin = nullptr) {
  switch (type) {
    case WS_CONNECTED:
      Serial.println("Client connected. Total clients: " + String(clients.size()));
      if(origin){
        String stateMsg = String("led/status:") + (ledState ? "on" : "off");
        origin->send(stateMsg);
      }
      break;
    case WS_DISCONNECTED:
      // Print count after removal (approximate): current size minus one
      Serial.println("Client disconnected. Total clients: " + String(clients.size() > 0 ? clients.size() - 1 : 0));
      break;
    case WS_MESSAGE:
      Serial.println("Received: " + payload);
      // handle LED commands
      if (payload == "led/on") {
        digitalWrite(LED_BUILTIN, HIGH);
        ledState = true;
        // broadcast LED state to all clients
        broadcastMessage("led/status:on");
      } else if (payload == "led/off") {
        digitalWrite(LED_BUILTIN, LOW);
        ledState = false;
        broadcastMessage("led/status:off");
      } else {
        // regular chat/message -> echo to all
        broadcastMessage(payload);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("\nStarting WebSocket Server on " + String(BOARD_NAME));

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module failed!");
    while (true);
  }

  Serial.print("Setting up Access Point: ");
  Serial.println(ssid);
  if (!WiFi.beginAP(ssid, password)) {
    Serial.println("Failed to start Access Point");
    while (true);
  }

  Serial.println("Waiting for AP to start...");
  unsigned long start = millis();
  while (WiFi.status() != WL_AP_LISTENING && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nAP started successfully.");

  IPAddress ip = WiFi.localIP();
  Serial.println("AP IP address: " + ip.toString());

  SocketsServer.listen(WEBSOCKETS_PORT);

  server.on("/", []() {
    String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>IoT LED Dashboard</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #e3f2fd, #ffffff);
      text-align: center;
      padding: 40px;
    }
    .card {
      background: white;
      padding: 30px;
      border-radius: 16px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.1);
      display: inline-block;
      min-width: 300px;
    }
    h2 {
      color: #1565c0;
      margin-bottom: 20px;
    }
    .switch {
      position: relative;
      display: inline-block;
      width: 70px;
      height: 38px;
      margin-top: 10px;
    }
    .switch input { display: none; }
    .slider {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background-color: #ccc;
      transition: .4s;
      border-radius: 38px;
    }
    .slider:before {
      position: absolute;
      content: "";
      height: 30px; width: 30px;
      left: 4px; bottom: 4px;
      background-color: white;
      transition: .4s;
      border-radius: 50%;
    }
    input:checked + .slider {
      background-color: #43a047;
    }
    input:checked + .slider:before {
      transform: translateX(32px);
    }
    .led-indicator {
      width: 24px;
      height: 24px;
      border-radius: 50%;
      display: inline-block;
      margin-left: 10px;
      vertical-align: middle;
      box-shadow: 0 0 10px rgba(0,0,0,0.2);
    }
    .led-on {
      background-color: #43a047;
      box-shadow: 0 0 15px #43a047;
    }
    .led-off {
      background-color: #e53935;
      box-shadow: 0 0 15px #e53935;
    }
    .status {
      font-size: 1.1em;
      margin-top: 10px;
      color: #333;
    }
    .sensor {
      margin-top: 12px;
      font-size: 1.05em;
      color: #333;
    }
    .sensor span {
      font-weight: bold;
    }
    .messages {
      margin-top: 20px;
      text-align: left;
      color: #555;
    }
    input[type="text"] {
      padding: 8px;
      border-radius: 8px;
      border: 1px solid #ccc;
      width: 70%;
      margin-right: 5px;
    }
    button {
      padding: 8px 14px;
      border: none;
      border-radius: 8px;
      background-color: #1565c0;
      color: white;
      cursor: pointer;
    }
    button:hover {
      background-color: #0d47a1;
    }
  </style>
</head>
<body>
  <div class="card">
    <h2>IoT LED Control</h2>
    <label class="switch">
      <input type="checkbox" id="ledToggle" onclick="toggleLED()">
      <span class="slider"></span>
    </label>
    <span id="ledCircle" class="led-indicator led-off"></span>
    <div class="status" id="ledStatus">LED is OFF</div>
    <div class="sensor">
      <div>Temperature: <span id="tempValue">--</span> C</div>
      <div>Humidity: <span id="humValue">--</span> %</div>
    </div>
    <hr>
    <input id="msg" type="text" placeholder="Type message">
    <button onclick="sendMessage()">Send</button>
    <div class="messages">
      <h3>Messages:</h3>
      <span id="received"></span>
    </div>
  </div>

  <script>
    var wsServer = 'ws://' + window.location.hostname + ':8080/';
    var connection = new WebSocket(wsServer);
    var lastCommandSentAt = null;

    connection.onmessage = function (e) {
      var receiveTime = performance.now();
      if (e.data.startsWith("led/status:")) {
        var state = e.data.split(":")[1];
        var ledCircle = document.getElementById('ledCircle');
        if (state === "on") {
          document.getElementById('ledStatus').innerText = "LED is ON";
          document.getElementById('ledToggle').checked = true;
          ledCircle.className = "led-indicator led-on";
        } else {
          document.getElementById('ledStatus').innerText = "LED is OFF";
          document.getElementById('ledToggle').checked = false;
          ledCircle.className = "led-indicator led-off";
        }
      } else if (e.data.startsWith("sensor/temp:")) {
        document.getElementById('tempValue').innerText = e.data.substring(12);
      } else if (e.data.startsWith("sensor/hum:")) {
        document.getElementById('humValue').innerText = e.data.substring(11);
      } else {
        document.getElementById('received').innerText += e.data + ' ';
      }
      console.log("Update RECEIVED at: " + receiveTime);
      if (lastCommandSentAt !== null) {
        console.log("Latency for this command: " + (receiveTime - lastCommandSentAt) + " ms");
      }
      console.log(performance.now());
    };

    function toggleLED() {
      var isOn = document.getElementById('ledToggle').checked;
      lastCommandSentAt = performance.now();
      console.log("Command SENT at: " + lastCommandSentAt);
      connection.send(isOn ? "led/on" : "led/off");
    }

    function sendMessage() {
      var msg = document.getElementById('msg').value;
      if (msg.trim() !== "") {
        lastCommandSentAt = performance.now();
        console.log("Command SENT at: " + lastCommandSentAt);
        connection.send(msg);
        document.getElementById('msg').value = '';
      }
    }
  </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", htmlPage);
  });

  server.begin();
}

void loop() {
  server.handleClient();

  // Accept new clients 
  if (SocketsServer.available()) {
    WebsocketsClient incoming = SocketsServer.accept();
    if (incoming.available()) {
      WebsocketsClient* nc = new WebsocketsClient;
      *nc = incoming;  
      delay(50);       
      clients.push_back({nc, millis(), false});

      
      nc->onMessage([](WebsocketsClient &client, WebsocketsMessage msg){
        touchClient(&client);
        String data = msg.data();
        handleWebSocketEvent(WS_MESSAGE, data, &client);
      });
      nc->onEvent([](WebsocketsClient &client, WebsocketsEvent evt, WSInterfaceString payload){
        touchClient(&client);
        (void)payload;
        if (evt == WebsocketsEvent::ConnectionClosed) {
          markDisconnected(&client);
        }
      });
      handleWebSocketEvent(WS_CONNECTED, "", nc);
    }
  }

  //  read incoming messages or detect disconnection
  for (int i = (int)clients.size() - 1; i >= 0; --i) {
    WebsocketsClient* c = clients[i].client;
    if (!c) {
      clients.erase(clients.begin() + i);
      continue;
    }

    if (clients[i].disconnected) {
      handleWebSocketEvent(WS_DISCONNECTED, "", c);
      c->close();
      delete c;
      clients.erase(clients.begin() + i);
      continue;
    }

    c->poll();
  }

  delay(1);
}

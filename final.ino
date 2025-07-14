    #include <ThingsBoard.h>
    #include <WiFi.h>
    #include <NewPing.h>
    #include <HTTPClient.h>
    #include <ArduinoJson.h>

    #define TRIG_PIN 5     // ESP32 pin GIOP26 connected to Ultrasonic Sensor's TRIG pin
    #define ECHO_PIN 18    // ESP32 pin GIOP25 connected to Ultrasonic Sensor's ECHO pin
    #define BUZZER_PIN 19  // Buzzer connected to digital pin D5
    #define LED_PIN 21     // ESP32 pin GIOP17 connected to LED's pin
    #define WIFI_STATUS_LED 2
    #define MAX_DISTANCE 400
    #define DISTANCE_THRESHOLD 100  // centimeters
    #define BUZZER_VOLUME 10       // Adjust this value to control the buzzer BUZZER_VOLUME

    constexpr char WIFI_SSID[] PROGMEM = "Redmi 9";
    constexpr char WIFI_PASSWORD[] PROGMEM = "12345678";
    constexpr char TOKEN[] PROGMEM = "uf6XYfN5AvhmkwRPoZIM";

    constexpr char THINGSBOARD_SERVER[] PROGMEM = "demo.thingsboard.io";
    constexpr uint16_t THINGSBOARD_PORT PROGMEM = 1883U;
    constexpr uint32_t MAX_MESSAGE_SIZE PROGMEM = 128U;
    constexpr uint32_t SERIAL_DEBUG_BAUD PROGMEM = 115200U;
    constexpr char DISTANCE_KEY[] PROGMEM = "Distance";
    constexpr char ACTUATOR_KEY[] PROGMEM = "actuator";
    constexpr const char OBSTACLE_TELEMETRY[] PROGMEM = "Detection";
    constexpr const char RPC_SWITCH_METHOD[] PROGMEM = "obstacle";

    WiFiClient espClient;
    ThingsBoard tb(espClient, MAX_MESSAGE_SIZE);
    int status = WL_IDLE_STATUS;  // the Wifi radio's status
    bool subscribed = false;
    NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);  // NewPing setup of pins and maximum distance.
    int distance, switch_state = 0;
    bool buzzerState = false;
     /// @brief Initalizes WiFi connection,
    // will endlessly delay until a connection has been successfully established
    void InitWiFi() {
      Serial.print("Attempting to connect to network: ");
      Serial.println(WIFI_SSID);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println("Connected to AP");
      Serial.print("ESP32 IP Address: ");
      Serial.println(WiFi.localIP());
    }
    bool reconnect() {
      // Check to ensure we aren't connected yet
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        return true;
      }
      // If we aren't establish a new connection to the given WiFi network
      InitWiFi();
      return true;
    }
    RPC_Response setServoSwitchState(RPC_Data &data) {
      Serial.println("RECIEVED SWITCH STATE");
      switch_state = data;
      // Serial.println("SWITCH STATE CHANGE:");
      // Serial.print(switch_state);
      return RPC_Response("actuator", 0);
    }

    const std::array<RPC_Callback, 1U> callbacks = {
      RPC_Callback{ RPC_SWITCH_METHOD, setServoSwitchState }
    };

    int getDistance() {
      delay(50);
      return sonar.ping_cm();
    }
    bool getLocationFromIP(float &lat, float &lon) {
    HTTPClient http;
    http.begin("http://ip-api.com/json");
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Response: " + response);
        StaticJsonDocument<512> doc;
        deserializeJson(doc, response);
        lat = doc["lat"];
        lon = doc["lon"];
        http.end();
        return true;
    }
    Serial.println("Failed to get location from IP.");
    http.end();
    return false;
    }
    void setup() {
      // Initalize serial connection for debugging
      Serial.begin(SERIAL_DEBUG_BAUD);
      pinMode(LED_PIN, OUTPUT);
      pinMode(BUZZER_PIN, OUTPUT);
      pinMode(WIFI_STATUS_LED, OUTPUT);
      digitalWrite(LED_PIN, LOW);
      analogWrite(BUZZER_PIN, 0);
      digitalWrite(WIFI_STATUS_LED, LOW);
      delay(1000);
      InitWiFi();
      HTTPClient http;
    http.begin("http://demo.thingsboard.io");  // Try accessing the server
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        Serial.print("ThingsBoard Server Response: ");
        Serial.println(httpCode);
    } else {
        Serial.println("Cannot reach ThingsBoard!");
    }

    http.end();
    }

    void loop() {
      delay(1000);

      if (!reconnect()) {
        return;
      }

      if (!tb.connected()) {
        // Reconnect to the ThingsBoard server,
        // if a connection was disrupted or has not yet been established
        Serial.printf("Connecting to: (%s) with token (%s)\n", THINGSBOARD_SERVER, TOKEN);
        digitalWrite(WIFI_STATUS_LED, LOW);
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
          Serial.println(F("Failed to connect"));
          return;
        }
      }

      if (!subscribed) {
        Serial.println("Subscribing for RPC...");
        // Perform a subscription. All consequent data processing will happen in
        // processTemperatureChange() and processSwitchChange() functions,
        // as denoted by callbacks array.
        if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
          Serial.println("Failed to subscribe for RPC");
          return;
        }
        Serial.println("Subscribe done");
        digitalWrite(WIFI_STATUS_LED, HIGH);
        subscribed = true;
      }

      distance = getDistance();
      tb.sendTelemetryInt(DISTANCE_KEY, distance);
      Serial.print("Distance = ");
      Serial.println(distance);

      if (distance <= 100 && distance > 0) { 
        Serial.println("Obstacle Detected!");
        tb.sendTelemetryString(OBSTACLE_TELEMETRY, "Obstacle Detected");
        tb.sendAttributeBool(ACTUATOR_KEY, true);
        digitalWrite(LED_PIN, HIGH);
        analogWrite(BUZZER_PIN, BUZZER_VOLUME);
        delay(500);
      } else {
        tb.sendTelemetryString(OBSTACLE_TELEMETRY, "Obstacle Not Detected");
        tb.sendAttributeBool(ACTUATOR_KEY, false);
        digitalWrite(LED_PIN, LOW);
        analogWrite(BUZZER_PIN, 0);
      }
      float latitude, longitude;
    if (getLocationFromIP(latitude, longitude)) {
        String payload = "{";
        payload += "\"latitude\": " + String(latitude, 6) + ",";
        payload += "\"longitude\": " + String(longitude, 6);
        payload += "}";
        Serial.print("Sending Location: ");
        Serial.println(payload);
        tb.sendTelemetryJson(payload.c_str());
    }
      tb.loop();
    }
/**
 * hrm-fan-control
 * ---------------
 * Replace regular AC fan speed switch (eg: low, mid, high) with a relay switch controlled by an ESP32 board, based on HR zones of a BLE HR Monitor.
 * 
 * Make sure change the ESP32 partition scheme to "Minimal SPIFFS" on Arduino editor.
 * 
 * Based on:
 * - GitHub agrabbs/hrm_fan_control by Andrew Grabbs
 * - ESP32 Async Web Server – Control Outputs with Arduino IDE by Random Nerd Tutorials
 * - ESP32 Flash Memory – Store Permanent Data (Write and Read) by Random Nerd Tutorials and control a relay author Andrew Grabbs
 */
                                                                                                                        // Comment Starting Point
// --- Include section ---
#include <stdarg.h>                                                                                                     // Load stdarg library
#include <EEPROM.h>                                                                                                     // Load EEPROM library
#include <WiFi.h>                                                                                                       // Load Wi-Fi library
#include <AsyncTCP.h>                                                                                                   // Load AsyncTCP library
#include <ESPAsyncWebServer.h>                                                                                          // Load ESPAsyncWebServer library
#include <BLEDevice.h>                                                                                                  // Load BLE library

// --- Global Define & Variables section ---
#define DEBUG_BLE true                                                                                                  // Print Serial Messages for BLE
#define DEBUG_BLE_DISCOVERY_NOTIFY false                                                                                // Print Serial Messages for BLE discovered servers & notify events
#define DEBUG_WIFI true                                                                                                 // Print Serial Messages for WIFI
#define DEBUG_EEPROM true                                                                                               // Print Serial Messages for EEPROM
#define EEPROM_HR_ZONE_1_SIZE 1                                                                                         // HR Zone 1 (byte)
#define EEPROM_HR_ZONE_2_SIZE 1                                                                                         // HR Zone 2 (byte)
#define EEPROM_WIFIMODE_SIZE 1                                                                                          // WiFi Mode (0: Not configured; 1: SoftAP; 2: AP Client)
#define EEPROM_SSID_SIZE 32                                                                                             // SSID lenght (31 + char(0))
#define EEPROM_PASSWORD_SIZE 64                                                                                         // WPA Password lenght (63 + char(0))
#define EEPROM_PAIREDBLE_SIZE 18                                                                                        // Paired BLE address lenght (17 + char(0))
#define LONG_LED_BLINK 1600                                                                                             // Long LED blink = 1600ms
#define SHORT_LED_BLINK 200                                                                                             // Short LED blink = 200ms
#define WIFI_TIMEOUT 20000                                                                                              // WiFi Timeout = 20s
#define BLE_TIMEOUT 5000                                                                                                // BLE Timeout = 5s
#define LOOP_DELAY 1000                                                                                                 // How often (in milliseconds) we run the main loop functions
#define RELAY_NO true                                                                                                   // Set to true to define Relay as Normally Open (NO)
#define BLE_LED 18                                                                                                      // Bluetooth LED GPIO
#define WIFI_LED 19                                                                                                     // WiFi LED GPIO
#define MANUAL_OVERRIDE_SWITCH 32                                                                                       // Manual Override Input GPIO (ie: Do not use HRM zones, use Fan Switch)
#define OVERRIDE_RELAY 33                                                                                               // Manual Override Relay
#define NUM_RELAYS 3                                                                                                    // Set number of relay switches
unsigned long gCurrentMillis = 0;                                                                                       // Stores the value of millis() in each iteration of loop()
byte gRelayGPIOs[NUM_RELAYS] = {25, 26, 27};                                                                            // Assign each GPIO to a relay

// --- BLE Global Variables section ---
char gPairedBLE[EEPROM_PAIREDBLE_SIZE];                                                                                 // Default BLE Server. Blank = Search for 1st BLE HRM available.
byte gHRZone1 = 105;                                                                                                    // Heart Rate Zone (<=) for Low speed
byte gHRZone2 = 135;                                                                                                    // Heart Rate Zone (<=) for Medium speed
boolean gRememberPairedBLE = false;                                                                                     // Flag if BLE has been saved in the EEPROM.
byte gCurrentHRZone = 0;                                                                                                // Current HR Zone reading.
unsigned long gNoHRMillis = 0;                                                                                          // First time we received 0 HR reading.
boolean gBLEInitiateConnection = false;                                                                                 // BLE initiate connection to server flag.
boolean gBLEConnected = false;                                                                                          // BLE connection status flag.
boolean gBLEScanInProgress = false;                                                                                     // BLE scanning in progress flag.
BLEUUID serviceUUID("0000180d-0000-1000-8000-00805f9b34fb");                                                            // Remote service we wish to connect to.
BLEUUID charUUID(BLEUUID((uint16_t)0x2A37));                                                                            // The characteristic of the remote service we are interested in (0x2A37).
BLEAdvertisedDevice* pMyDevice;                                                                                         // BLE Server
BLERemoteCharacteristic* pRemoteCharacteristic;                                                                         // BLE Remote Characteristic
BLEClient* pClientBLE;                                                                                                  // BLE Client

// --- WiFi Global Variables section ---
byte gWiFiMode = WIFI_AP;                                                                                               // Default WiFi Mode, when there is no saved settings (WIFI_AP or WIFI_STA).
char gSSID[EEPROM_SSID_SIZE] = "HRM-Fan-Control-AP";                                                                    // Default SSID, when there is no saved settings
char gPassword[EEPROM_PASSWORD_SIZE] = "1234567890";                                                                    // Default Password, when there is no saved settings
boolean gWiFiConnected = false;                                                                                         // WiFi connection status.
boolean gWiFiConnectionInProgress = false;                                                                              // Initiating an WiFi connection.
boolean gServerStarted = false;                                                                                         // HTTP Server Started Status.
IPAddress gIP;                                                                                                          // Device IP address.
AsyncWebServer server(80);                                                                                              // Web Server on port 80 (HTTP)
const char HIDDEN_PASSWORD_CHAR[] = "´";                                                                                // Hidden Char for Password
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>HRM Fan Control - Settings</title>
  <meta name=viewport content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; font-size: 10pt; display: inline-block; text-align: center}
    body {margin:0px auto; padding-bottom: 25px; padding-top: 25px}
    input[type=text] {width: 160px}
    input[type=password] {width: 160px}
    input[type=number] {width: 45px; text-align=center}
    input[type=button] {width: 160px}
    select {width: 168px}
  </style>
</head>
<body>
  <form action=/save method=POST id=Settings>
    <table align=center width=600 border=2 cellspacing=0 cellpadding=5>
      <tr bgcolor="#000099">
        <td align=center style="font-size: 20pt;"><font color=white><b>HRM Fan Control - Settings</b></font></td>
      </tr>
      <tr>
        <td align=center>
          <table width=500>
%CONFIGPLACEHOLDER%
            <tr>
              <td colspan=2 align=center><hr width=500></td>
            </tr>
            <tr>
              <td colspan=2 align=center><input type=button value="Save Settings" onclick="ConfirmAction('save')"><input type=button
                value="Disconnect BLE HRM" onclick="ConfirmAction('disconnectble')"></td>
            </tr>
            <tr>
              <td colspan=2 align=center><input type=button value="Restart" onclick="ConfirmAction('restart')"><input type=button
                value="Factory Reset" onclick="ConfirmAction('factory')"></td>
            </tr>
          </table>
        </td>
      </tr>
    </table>
  </form>
<script>
function Validate() {
  if(document.getElementById("SSID").value.length < 3) {
    alert("Minimum SSID lenght is 3 characters.");
    return false;
  }
  if(document.getElementById("Password").value.length < 8 && document.getElementById("Password").value.length > 0) {
    alert("Minimum password lenght is 8 characters. Or leave it blank for no password.");
    return false;
  }
  if(document.getElementById("RememberBLE").checked && document.getElementById("PairedBLE").value == "") {
    alert("Remember BLE HRM option is only valid when connected to a BLE HRM.");
    return false;
  }
  if(isNaN(document.getElementById("gHRZone1").value) || isNaN(document.getElementById("gHRZone2").value)) {
    alert("Only numeric values allowed for HR Zones.");
    return false;
  }
  if(document.getElementById("gHRZone2").value < document.getElementById("gHRZone1").value) {
    alert("HR Zone 2 has to be higher than HR Zone 1.");
    return false;
  }
  if(document.getElementById("gHRZone1").value <= 0) {
    alert("HR Zone 1 has to be greater than 0.");
    return false;
  }
  if(document.getElementById("gHRZone1").value <= 0) {
    alert("HR Zone 1 has to be greater than 0.");
    return false;
  }
  if(document.getElementById("gHRZone2").value > 250) {
    alert("HR Zone 2 can't be greater than 250.");
    return false;
  }
  return true;
}
function ConfirmAction(button) {
  switch(button) {
    case "save":
      if(Validate()) {
        if(confirm("Are you sure you want to save the configuration?\n\nThe device will restart with the new settings.")) {
          document.getElementById("Settings").submit();
        }
      }
      break;
    case "disconnectble":
      if(document.all.PairedBLE.value == "") {
        alert("No previously paired BLE HRM. Wait for a connection to be established and refresh this page.")
      } else {
        if(confirm("Are you sure you want to disconnect from the current BLE HRM?\n\nThe device will restart scanning and connect to the first available BLE HRM.")) {
          location.href = "/disconnectble";
        }
      }
      break;
    case "restart":
      if(confirm("Are you sure you want restart the device?\n\nAny unsaved configuration will be lost!")) {
        location.href = "/restart";
      }
      break;
    case "factory":
      if(confirm("Are you sure you erase all setting and perform a factory reset?\n\nAll settings will be erased!")) {
        if(confirm("Are you really sure?\n\nThe device will restart with the factory default settings.")) {
          location.href = "/factoryreset";
        }
      }
      break;
  }
}
</script>
</body>
</html>
)rawliteral";
const char RESTART_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>HRM Fan Control - Restarting</title>
  <meta name=viewport content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; font-size: 10pt; display: inline-block; text-align: center;}
    body {margin:0px auto; padding-bottom: 25px; padding-top: 25px;}
  </style>
</head>
<body>
  <table align=center width=600 border=2 cellspacing=0 cellpadding=5>
    <tr bgcolor="#000099">
      <td align=center style="font-size: 20pt;"><font color=white><b>HRM Fan Control</b></font></td>
    </tr>
      <tr>
        <td align=center><p>Device is restarting...</p>
          <p>Wait for <span id=SpanTimer>10</span> seconds for it to complete and you will be redirected to the index page.</p>
          <p>If the wireless settings have been changed, the device might have a different IP address, therefore the redirect won't work.</p></td>
      </tr>
    </table>
  </form>
<script>
function CountdownTimer() {
  if(timeleft <=0) {
    clearInterval(Timer);
    location.href = "/";
  }
  document.getElementById("SpanTimer").innerHTML = --timeleft;
}
var timeleft = 10;
var Timer = setInterval(CountdownTimer, 1000);
</script>
</body>
</html>
)rawliteral";

void DebugPrint(const char* moduleName, const char* messageBody, ...) {
  char buffer[256];
  va_list args;
  if(!((!strcmp("BLE", moduleName) && DEBUG_BLE) || (!strcmp("WIFI", moduleName) && DEBUG_WIFI) || (!strcmp("EEPROM", moduleName) && DEBUG_EEPROM)
      || (!strcmp("BLE|DISCOVERY", moduleName) && DEBUG_BLE_DISCOVERY_NOTIFY) || (!strcmp("BLE|NOTIFY", moduleName) && DEBUG_BLE_DISCOVERY_NOTIFY))) {
    return;
  } else {
    va_start(args, messageBody);
    vsprintf(buffer, messageBody, args);
    Serial.print("[");
    Serial.print(moduleName);
    Serial.print("] ");
    Serial.println(buffer);
    va_end(args);
  }
}

class cAccessEEPROM {
  public:
    void SaveSettings(boolean remember) {
      DebugPrint("EEPROM", "Saving configuration to EEPROM...");
      int addr = 0;
      EEPROM.write(addr++, gHRZone1);                                                                                   // Write HR Zone 1 to EEPROM.
      EEPROM.write(addr++, gHRZone2);                                                                                   // Write HR Zone 2 to EEPROM.
      EEPROM.write(addr++, gWiFiMode);                                                                                  // Write WiFi Mode to EEPROM.
      for(int i = 0; i < EEPROM_SSID_SIZE; i++) {                                                                       // Write SSID to EEPROM.
        EEPROM.write(addr++, gSSID[i]);
      }
      for(int i = 0; i < EEPROM_PASSWORD_SIZE; i++) {                                                                   // Write Password to EEPROM.
        EEPROM.write(addr++, gPassword[i]);
      }
      for(int i = 0; i < EEPROM_PAIREDBLE_SIZE; i++) {                                                                  // Write Paired BLE HRM to EEPROM.
        if(remember) {
          EEPROM.write(addr++, gPairedBLE[i]);
        } else {
          EEPROM.write(addr++, 0);
        }
      }
      gRememberPairedBLE = remember;
      EEPROM.commit();
      DebugPrint("EEPROM", " -> Done.");
    }

    void DisconnectHRM() {
      DebugPrint("EEPROM", "Erasing HRM BLE configuration from EEPROM...");
      int addr = EEPROM_HR_ZONE_1_SIZE + EEPROM_HR_ZONE_2_SIZE + EEPROM_WIFIMODE_SIZE + EEPROM_SSID_SIZE + EEPROM_PASSWORD_SIZE;
      for(int i = 0; i < EEPROM_PAIREDBLE_SIZE; i++) {                                                                  // Write Paired BLE HRM to EEPROM.
        EEPROM.write(addr++, 0);
        gPairedBLE[i] = 0;
        }
      EEPROM.commit();
      gRememberPairedBLE = false;
      DebugPrint("EEPROM", " -> Done.");
      if(gBLEConnected) {
        DebugPrint("EEPROM", " -> Disconnecting from BLE HRM...");
        pClientBLE->disconnect();
      }
    }

    void FactoryReset() {
      DebugPrint("EEPROM", "Saving wireless configuration to EEPROM...");
      int addr = 0;
      for(int i = 0; i < EEPROM_HR_ZONE_1_SIZE + EEPROM_HR_ZONE_2_SIZE + EEPROM_WIFIMODE_SIZE + EEPROM_SSID_SIZE + EEPROM_PASSWORD_SIZE
          + EEPROM_PAIREDBLE_SIZE; i++) {                                       // Erase all settings from EEPROM.
        EEPROM.write(addr++, 255);
        }
      EEPROM.commit();
      DebugPrint("EEPROM", " -> Done.");
    }
                                                                                                                        // Comment Starting Point

    void ReadConfig() {
      DebugPrint("EEPROM", "Initialising EEPROM...");
      if(!EEPROM.begin(EEPROM_HR_ZONE_1_SIZE + EEPROM_HR_ZONE_2_SIZE + EEPROM_WIFIMODE_SIZE + EEPROM_SSID_SIZE +
          EEPROM_PASSWORD_SIZE + EEPROM_PAIREDBLE_SIZE)) {                                                              // Try to start EEPROM.
        DebugPrint("EEPROM", "Failed to initialize EEPROM! Running with default values.");
      } else {                                                                                                          // Success, read EEPROM values.
        DebugPrint("EEPROM", "EEPROM Initialized!");
        if(EEPROM.read(0) == 255) {                                                                                     // Not configured, start with default values.
          DebugPrint("EEPROM", "Configuration not found, running with default values.");
        } else {
          DebugPrint("EEPROM", "Configuration found, reading values...");
          int addr = 0;
          gHRZone1 = EEPROM.read(addr++);                                                                               // Read HR Zone 1 from EEPROM.
          gHRZone2 = EEPROM.read(addr++);                                                                               // Read HR Zone 2 from EEPROM.
          gWiFiMode = EEPROM.read(addr++);                                                                              // Read WiFi Mode from EEPROM.
          for(int i = 0; i < EEPROM_SSID_SIZE; i++) {                                                                   // Read SSID from EEPROM.
            gSSID[i] = char(EEPROM.read(addr++));
            }
          for(int i = 0; i < EEPROM_PASSWORD_SIZE; i++) {                                                               // Read Password from EEPROM.
            gPassword[i] = char(EEPROM.read(addr++));
            }
          for(int i = 0; i < EEPROM_PAIREDBLE_SIZE; i++) {                                                              // Read Paired BLE HRM from EEPROM.
            gPairedBLE[i] = char(EEPROM.read(addr++));
            }
          if(gPairedBLE[0] != 0) {
            gRememberPairedBLE = true;
          } else {
            gRememberPairedBLE = false;
          }
          DebugPrint("EEPROM", " -> Done.");
        }
        DebugPrint("EEPROM", "Initializing with the following values:");
        DebugPrint("EEPROM", " -> WiFi Mode: [%u]", gWiFiMode);
        DebugPrint("EEPROM", " -> SSID: [%s]", gSSID);
        DebugPrint("EEPROM", " -> BLE HRM: [%s]", gPairedBLE);
        DebugPrint("EEPROM", " -> HR Zone 1/2 Threshold: [%u]", gHRZone1);
        DebugPrint("EEPROM", " -> HR Zone 2/3 Threshold: [%u]", gHRZone2);
        DebugPrint("EEPROM", " -> Done.");
      }
    }
  };
cAccessEEPROM oAccessEEPROM;                                                                                            // EEPROM Object

class cWiFiWebService {
  private:
    unsigned long previousWiFiLoopMillis = 0;                                                                           // Last time the WiFi Loop was run.
    unsigned long previousWiFiBlinkMillis = 0;                                                                          // Last time the WiFi LED blink was turned on.
    boolean WiFiLEDOn = false;                                                                                          // LED On status.
  
    void BlinkLED() {
      if(gWiFiMode == WIFI_STA) {                                                                                       // WiFi Client mode.
        if(gWiFiConnected) {                                                                                            // Connected to AP. Short Blink on, Long blink off
          if(WiFiLEDOn) {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= SHORT_LED_BLINK) {                                         // Turn WiFi LED off
              digitalWrite(WIFI_LED, LOW);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = false;
            }
          } else {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= LONG_LED_BLINK) {                                          // Turn WiFi LED on
              digitalWrite(WIFI_LED, HIGH);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = true;
            }
          }
        } else {                                                                                                        // Connecting to AP. Short Blink on, Short blink off
          if(WiFiLEDOn) {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= SHORT_LED_BLINK) {                                         // Turn WiFi LED off
              digitalWrite(WIFI_LED, LOW);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = false;
            }
          } else {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= SHORT_LED_BLINK) {                                         // Turn WiFi LED on
              digitalWrite(WIFI_LED, HIGH);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = true;
            }
          }
        }
      } else {                                                                                                          // WiFi AP mode, advertising SSID.
        if(gWiFiConnected) {                                                                                            // Long Blink on, Short blink off
          if(WiFiLEDOn) {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= LONG_LED_BLINK) {                                          // Turn WiFi LED off
              digitalWrite(WIFI_LED, LOW);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = false;
            }
          } else {
            if((gCurrentMillis - previousWiFiBlinkMillis) >= SHORT_LED_BLINK) {                                         // Turn WiFi LED on
              digitalWrite(WIFI_LED, HIGH);
              previousWiFiBlinkMillis = gCurrentMillis;
              WiFiLEDOn = true;
            }
          }
        }
      }
    }
  
    void StartHTTPServer() {
      DebugPrint("WIFI", "Starting WebServer listeners... ");
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {                                                     // Listen for "/" requests.
        DebugPrint("WIFI", "Providing WebServer page \"/\"... ");
        request->send_P(200, "text/html", INDEX_HTML, ParseHTML);
        delay(500);
      });
      server.on("/save", HTTP_POST, [] (AsyncWebServerRequest *request) {                                               // Listen for "/update" requests.
        DebugPrint("WIFI", "Providing WebServer page \"/save\"... ");
        char newSSID[EEPROM_SSID_SIZE];
        char newPassword[EEPROM_PASSWORD_SIZE];
        boolean RememberBLE = false;
        if (request->hasParam("WiFiMode", true) && request->hasParam("SSID", true) && request->hasParam("Password", true) 
            && request->hasParam("gHRZone1", true) && request->hasParam("gHRZone2", true)) {
          gWiFiMode = atoi(request->getParam("WiFiMode", true)->value().c_str());
          strcpy(newSSID, request->getParam("SSID", true)->value().c_str());
          strcpy(newPassword, request->getParam("Password", true)->value().c_str());
          gWiFiMode = atoi(request->getParam("WiFiMode", true)->value().c_str());
          gHRZone1 = atoi(request->getParam("gHRZone1", true)->value().c_str());
          gHRZone2 = atoi(request->getParam("gHRZone2", true)->value().c_str());
          if(request->hasParam("RememberBLE", true)) {
            RememberBLE = true;
          } else {
            RememberBLE = false;
          }
          if(newSSID[0] != 0) {
            strcpy(gSSID, newSSID);
          }
          if(newPassword[0] != HIDDEN_PASSWORD_CHAR[0]) {
            strcpy(gPassword, newPassword);
          }
          oAccessEEPROM.SaveSettings(RememberBLE);
        }
        request->send_P(200, "text/html", RESTART_HTML);
        delay(500);
        DebugPrint("WIFI", "Restarting the device... ");
        ESP.restart();
      });
      server.on("/disconnectble", HTTP_GET, [](AsyncWebServerRequest *request) {                                        // Listen for "/disconnectble" requests.
        DebugPrint("WIFI", "Providing WebServer page \"/disconnectble\"... ");
        oAccessEEPROM.DisconnectHRM();
        request->redirect("/");
      });
      server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {                                              // Listen for "/restart" requests.
        DebugPrint("WIFI", "Providing WebServer page \"/restart\"... ");
        request->send_P(200, "text/html", RESTART_HTML);
        delay(500);
        DebugPrint("WIFI", "Restarting the device... ");
        ESP.restart();
      });
      server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest *request) {                                         // Listen for "/factoryreset" requests.
        DebugPrint("WIFI", "Providing WebServer page \"/factory\"... ");
        request->send_P(200, "text/html", RESTART_HTML);
        delay(500);
        oAccessEEPROM.FactoryReset();
        DebugPrint("WIFI", "Restarting the device... ");
        ESP.restart();
      });
      server.begin();
      gServerStarted = true;
      DebugPrint("WIFI", " -> Done!");
   }

  void ConnectWiFi() {
    if(gWiFiMode == WIFI_STA) {                                                                                         // WiFi Client mode.
      DebugPrint("WIFI", "Connecting to WiFi AP...");
      WiFi.begin(gSSID, gPassword);                                                                                     // Trying to connect.
      gWiFiConnectionInProgress = true;
    } else {                                                                                                            // WiFi AP mode.
      DebugPrint("WIFI", "Starting AP hotspot...");
      WiFi.softAP(gSSID, gPassword);                                                                                    // Create an AP hotspot.
      gWiFiConnected = true;
      gIP = WiFi.softAPIP();                                                                                            // Store Device IP address.
      DebugPrint("WIFI", " -> IP address: [%u.%u.%u.%u]", gIP[0], gIP[1], gIP[2], gIP[3]);
    }
  }

  public:
    static void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
      DebugPrint("WIFI", "Client MAC address: %02X:%02X:%02X:%02X:%02X:%02X connected.",
                 info.sta_connected.mac[0],info.sta_connected.mac[1], info.sta_connected.mac[2],
                 info.sta_connected.mac[3], info.sta_connected.mac[4], info.sta_connected.mac[5]);
    }

    static void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
      DebugPrint("WIFI", "Client MAC address: %02X:%02X:%02X:%02X:%02X:%02X disconnected.",
                 info.sta_connected.mac[0],info.sta_connected.mac[1], info.sta_connected.mac[2],
                 info.sta_connected.mac[3], info.sta_connected.mac[4], info.sta_connected.mac[5]);
    }

    static void StationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
      DebugPrint("WIFI", "Connected to AP successfully!");
    }
    
    static void StationGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
      gWiFiConnected = true;
      gWiFiConnectionInProgress = false;
      gIP = WiFi.localIP();                                                                                             // Store Device IP address.
      DebugPrint("WIFI", " -> IP address: [%u.%u.%u.%u]", gIP[0], gIP[1], gIP[2], gIP[3]);
    }
    
    static void StationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
      server.end();
      gWiFiConnected = false;
      gWiFiConnectionInProgress = false;
      gServerStarted = false;
      DebugPrint("WIFI", "Disconnected from WiFi access point.");
      DebugPrint("WIFI", " -> WiFi lost connection. Reason: %u", info.disconnected.reason);
    }

    static String ParseHTML(const String& var) {
      String htmlcode = "";
      String txtAPHotSpot = "";
      String txtAPStation = "";
      String txtHiddenPassword = "";
      String txtRememberBLE = "";
      if(gWiFiMode == WIFI_AP) {
        txtAPHotSpot = "selected";
      } else {
        txtAPStation = "selected";
      }
      for(int i=0; i<EEPROM_PASSWORD_SIZE; i++) {
        if(gPassword[i] == 0) {
          break;
        } else {
          txtHiddenPassword += HIDDEN_PASSWORD_CHAR[0];
        }
      }
      if(gRememberPairedBLE) {
        txtRememberBLE = "checked";
      }
      if(var == "CONFIGPLACEHOLDER") {
        htmlcode += "            <tr>\n";
        htmlcode += "              <td colspan=2 align=center><hr width=500>\n";
        htmlcode += "                <b>Wireless</b>\n";
        htmlcode += "                <hr width=500></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right>Mode:</td>\n";
        htmlcode += "              <td align=left><select name=WiFiMode>\n";
        htmlcode += "                <option "+txtAPHotSpot+" value=\""+String(WIFI_AP)+"\">Access Point (Hotspot)</option>\n";
        htmlcode += "                <option "+txtAPStation+" value=\""+String(WIFI_STA)+"\">Station</option>\n";
        htmlcode += "                </select></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right>SSID:</td>\n";
        htmlcode += "              <td align=left><input type=text name=SSID id=SSID value=\""+String(gSSID)+"\" maxlength="+String(EEPROM_SSID_SIZE - 1)+"></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right>Password:</td>\n";
        htmlcode += "              <td align=left><input type=password name=Password id=Password value=\""+txtHiddenPassword+"\" maxlength=\""+String(EEPROM_PASSWORD_SIZE - 1)+"\"></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td colspan=2 align=center><hr width=500>\n";
        htmlcode += "                <b>BLE HRM</b>\n";
        htmlcode += "                <hr width=500></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right><b>Paired BLE:</b></td>\n";
        htmlcode += "              <td align=left><input type=text id=PairedBLE name=PairedBLE value=\""+String(gPairedBLE)+"\" maxlength=\""+String(EEPROM_PAIREDBLE_SIZE - 1)+"\" readonly></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right><b>Status:</b></td>\n";
        htmlcode += "              <td align=left>";
        if(gBLEInitiateConnection) {
          htmlcode += "                Connecting...";
        } else if(gBLEConnected) {
          htmlcode += "                Connected!";
        } else if(gBLEScanInProgress) {
          htmlcode += "                Scanning...";
        } else {
          htmlcode += "                Not doing anything.";
        }
        htmlcode += "              </td>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right><b>Current HR Zone:</b></td>\n";
        htmlcode += "              <td align=left>"+String(gCurrentHRZone)+"</td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td align=right><b>Remember:</b></td>\n";
        htmlcode += "              <td align=left><input type=checkbox "+txtRememberBLE+" id=RememberBLE name=RememberBLE value=1></td>\n";
        htmlcode += "            </tr>\n";
        htmlcode += "            <tr>\n";
        htmlcode += "              <td colspan=2 align=center>\n";
        htmlcode += "                <table width=500>\n";
        htmlcode += "                  <tr>\n";
        htmlcode += "                    <td colspan=6 align=center><hr width=500>\n";
        htmlcode += "                      <b>Heart Rate Zones / Fan Speed</b>\n";
        htmlcode += "                      <hr width=500></td>\n";
        htmlcode += "                  </tr>\n";
        htmlcode += "                  <tr>\n";
        htmlcode += "                    <td align=center><b>Fan Speed:</b></td>\n";
        htmlcode += "                    <td align=center>Low</td>\n";
        htmlcode += "                    <td align=center>/</td>\n";
        htmlcode += "                    <td align=center>Mid</td>\n";
        htmlcode += "                    <td align=center>/</td>\n";
        htmlcode += "                    <td align=center>High</td>\n";
        htmlcode += "                  </tr>\n";
        htmlcode += "                  <tr>\n";
        htmlcode += "                    <td align=center><b>HR Zones:</b></td>\n";
        htmlcode += "                    <td align=center>Zone 1</td>\n";
        htmlcode += "                    <td align=center><input type=number name=gHRZone1 id=gHRZone1 value=\""+String(gHRZone1)+"\"></td>\n";
        htmlcode += "                    <td align=center>Zone 2</td>\n";
        htmlcode += "                    <td align=center><input type=number name=gHRZone2 id=gHRZone2 value=\""+String(gHRZone2)+"\"></td>\n";
        htmlcode += "                    <td align=center>Zone 3</td>\n";
        htmlcode += "                  </tr>\n";
        htmlcode += "                </table>\n";
        htmlcode += "              </td>\n";
        htmlcode += "            </tr>\n";
      }
      return htmlcode;
    }

    void setup() {
      pinMode(WIFI_LED, OUTPUT);
      digitalWrite(WIFI_LED, HIGH);
      WiFiLEDOn = true;
    }

    void loop() {
      BlinkLED();
      if((gCurrentMillis - previousWiFiLoopMillis) >= LOOP_DELAY) {                                                     // Only run the WiFi loop as per previously set delay.
        previousWiFiLoopMillis = gCurrentMillis;
        if(!gWiFiConnected) {                                                                                           // If WiFi is not connected, start connection.
          if(!gWiFiConnectionInProgress) {
            ConnectWiFi();
          }
        } else if(!gServerStarted) {                                                                                    // If HTTP Server is not yet started, start it.
          StartHTTPServer();
        }
      }
    }
  };
cWiFiWebService oWiFiWebService;                                                                                         // WiFi Object

class cBLEClientService {
  private:
    unsigned long previousBLELoopMillis = 0;                                                                            // Last time the BLE Loop was run.
    unsigned long previousBLEBlinkMillis = 0;                                                                           // Last time the BLE LED blink was turned on.
    boolean BtLEDOn = false;                                                                                            // LED On status.
    byte BlinkTimes = 0;                                                                                                // Number of times the LED has blinked to represent current HR zone.
    boolean SetBLENotification = false;                                                                                 // Set BLE Notification flag.
  
    class MyClientCallback : public BLEClientCallbacks {
      void onConnect(BLEClient* pclient) {
      }
    
      void onDisconnect(BLEClient* pclient) {
        gBLEConnected = false;
        gNoHRMillis = 0;
        if(!gRememberPairedBLE) {
          strcpy(gPairedBLE, "");
        }
        DebugPrint("BLE", "Disconnected from the BLE HRM.");
      }
    };
    
    class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
      void onResult(BLEAdvertisedDevice advertisedDevice) {                                                             // Called for each advertising BLE server.
        DebugPrint("BLE|DISCOVERY", "BLE Advertised Device found: %s", advertisedDevice.getAddress().toString().c_str());
        if(advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(serviceUUID)) {                                                       // Device found, checking if it contains the service we are looking for.
          DebugPrint("BLE", "BLE %s has the service we want.", advertisedDevice.getAddress().toString().c_str());
          if(gPairedBLE[0] == 0) {                                                                                      // No paired device, connect to first HRM monitor available.
            DebugPrint("BLE", "No previously paired HRM BLE, connecting to this one.");
            gBLEInitiateConnection = true;
          } else {                                                                                                      // Test if the devices is the previously paired HRM BLE.
            if(strcmp(advertisedDevice.getAddress().toString().c_str(), gPairedBLE) == 0) {
              DebugPrint("BLE","This is the previously paired HRM BLE, connecting to this one.");
              gBLEInitiateConnection = true;
            } else {                                                                                                    // Not the device we are looking for.
              DebugPrint("BLE", "Not the previously paired HRM BLE. Keep scanning...");
            }
          }
          if(gBLEInitiateConnection) {
            DebugPrint("BLE", "Scanning stopped.");
            BLEDevice::getScan()->stop();                                                                               // Stop Scanning.
            gBLEScanInProgress = false;
            pMyDevice = new BLEAdvertisedDevice(advertisedDevice);
            strcpy(gPairedBLE, advertisedDevice.getAddress().toString().c_str());                                       // Save the paired BLE server
          }
        }
      }
    };
  
    void BlinkLED() {
      if(!gBLEConnected) {                                                                                              // Searching for BLE Servers. Short Blink on, Short blink off
        if(BtLEDOn) {
          if((gCurrentMillis - previousBLEBlinkMillis) >= SHORT_LED_BLINK) {                                            // Turn BLE LED off
            digitalWrite(BLE_LED, LOW);
            previousBLEBlinkMillis = gCurrentMillis;
            BtLEDOn = false;
          }
        } else {
          if((gCurrentMillis - previousBLEBlinkMillis) >= SHORT_LED_BLINK) {                                            // Turn BLE LED on
            digitalWrite(BLE_LED, HIGH);
            previousBLEBlinkMillis = gCurrentMillis;
            BtLEDOn = true;
          }
        }
      } else {
        if(!gBLEInitiateConnection) {                                                                                   // Connected. Short Blink on, Long blink off. Number of Short Blink according to HR current zone.                                                        
          if(BtLEDOn) {
            if(BlinkTimes < gCurrentHRZone) {
              if((gCurrentMillis - previousBLEBlinkMillis) >= SHORT_LED_BLINK) {                                        // Turn BLE LED off
                digitalWrite(BLE_LED, LOW);
                previousBLEBlinkMillis = gCurrentMillis;
                BtLEDOn = false;
              }           
            } else {
              if((gCurrentMillis - previousBLEBlinkMillis) >= LONG_LED_BLINK) {                                         // Turn BLE LED off
                digitalWrite(BLE_LED, LOW);
                previousBLEBlinkMillis = gCurrentMillis;
                BtLEDOn = false;
                BlinkTimes = 0;
              }
            }
          } else {
            if((gCurrentMillis - previousBLEBlinkMillis) >= SHORT_LED_BLINK) {                                          // Turn BLE LED on
              digitalWrite(BLE_LED, HIGH);
              previousBLEBlinkMillis = gCurrentMillis;
              BtLEDOn = true;
              BlinkTimes++;
            }
          }
        } else {                                                                                                        // Connecting. Long Blink on, Short blink off
          if(BtLEDOn) {
            if((gCurrentMillis - previousBLEBlinkMillis) >= SHORT_LED_BLINK) {                                          // Turn BLE LED off
              digitalWrite(BLE_LED, LOW);
              previousBLEBlinkMillis = gCurrentMillis;
              BtLEDOn = false;
            }
          } else {
            if((gCurrentMillis - previousBLEBlinkMillis) >= LONG_LED_BLINK) {                                           // Turn BLE LED on
              digitalWrite(BLE_LED, HIGH);
              previousBLEBlinkMillis = gCurrentMillis;
              BtLEDOn = true;
            }
          }
        }
      }
    }
  
    void ProcessHRZone() {
      if(gCurrentHRZone != 0) {
        for(int i=0; i<NUM_RELAYS; i++){
          if(RELAY_NO) {                                                                                                // If set to Normally Open (NO), the relay is off when you set the relay to HIGH.
            digitalWrite(gRelayGPIOs[i], HIGH);
          } else {
            digitalWrite(gRelayGPIOs[i], LOW);
          }
        }
        if(RELAY_NO) {                                                                                                  // If set to Normally Open (NO), the relay is off when you set the relay to HIGH.
          digitalWrite(gRelayGPIOs[gCurrentHRZone-1], LOW);
        } else {
          digitalWrite(gRelayGPIOs[gCurrentHRZone-1], HIGH);
        }
      } else {
        if(((gCurrentMillis - gNoHRMillis) >= BLE_TIMEOUT) && gNoHRMillis != 0) {
          for(int i=0; i<NUM_RELAYS; i++){
            if(RELAY_NO) {                                                                                              // If set to Normally Open (NO), the relay is off when you set the relay to HIGH.
              digitalWrite(gRelayGPIOs[i], HIGH);
            } else {
              digitalWrite(gRelayGPIOs[i], LOW);
            }
          }
          if(gBLEConnected) {
            DebugPrint("BLE", "No HR timeout reached, disconnecting from BLE HRM...");
            pClientBLE->disconnect();
          }
        }
      }
    }
  
    boolean connectToServer() {
      DebugPrint("BLE", "Forming a connection to %s.", pMyDevice->getAddress().toString().c_str());
      DebugPrint("BLE", " -> Creating client...");
      pClientBLE  = BLEDevice::createClient();
      DebugPrint("BLE", " -> Client created. Setting Callback...");
      pClientBLE->setClientCallbacks(new MyClientCallback());
      DebugPrint("BLE", " -> Callback set. Connecting to HRM server...");
      pClientBLE->connect(pMyDevice);                                                                                   // Connect to the remove BLE Server.
      while(!pClientBLE->isConnected()) {
        delay(200);
      }
      DebugPrint("BLE", " -> Connected to HRM server. Getting service UUID...");
      BLERemoteService* pRemoteService = pClientBLE->getService(serviceUUID);                                           // Obtain a reference to the service we are after in the remote BLE server.
      if(pRemoteService == nullptr) {
        DebugPrint("BLE", "Failed to find our service UUID: %s.", serviceUUID.toString().c_str());
        pClientBLE->disconnect();
        return false;
      }
      DebugPrint("BLE", " -> Found our service. Getting the characteristic we are after...");
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);                                              // Obtain a reference to the characteristic in the service of the remote BLE server.
      if(pRemoteCharacteristic == nullptr) {
        DebugPrint("BLE", "Failed to find our characteristic UUID: %s.", charUUID.toString().c_str());
        pClientBLE->disconnect();
        return false;
      }
      DebugPrint("BLE", " -> Found our characteristic. Registering for notification...");
      if(pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        DebugPrint("BLE", " -> Registered for notification. All done!");
      } else {
        DebugPrint("BLE", " -> Notification registration failed.");
      }
      gBLEConnected = true;
      SetBLENotification = false;
      return true;
    }

  public:
    static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
        uint8_t* pData, size_t length, boolean isNotify) {
      int previousHRZone = gCurrentHRZone;
      DebugPrint("BLE|NOTIFY", "Heart Rate: %ubpm", pData[1]);
      if(pData[1] <= gHRZone1 && pData[1] > 0) {
        gCurrentHRZone = 1;
      } else if(pData[1] > gHRZone1 && pData[1] <= gHRZone2) {
        gCurrentHRZone = 2;
      } else if(pData[1] > gHRZone2) {
        gCurrentHRZone = 3;
      } else {
        gCurrentHRZone = 0;
        if(gCurrentHRZone != previousHRZone) {
          gNoHRMillis = gCurrentMillis;
        }
      }
      if(gCurrentHRZone != previousHRZone) {
        DebugPrint("BLE", "New Heart Rate Zone: %u", gCurrentHRZone);
      }
    }

    void setup() {
      for(int i=0; i<NUM_RELAYS; i++) {                                                                                 // Set all relays to off when the program starts.
        pinMode(gRelayGPIOs[i], OUTPUT);
        if(RELAY_NO) {                                                                                                  // If set to Normally Open (NO), the relay is off when you set the relay to HIGH.
          digitalWrite(gRelayGPIOs[i], HIGH);
        } else {
          digitalWrite(gRelayGPIOs[i], LOW);
        }
      }
      pinMode(BLE_LED, OUTPUT);
      digitalWrite(BLE_LED, HIGH);
      pinMode(OVERRIDE_RELAY, OUTPUT);
      pinMode(MANUAL_OVERRIDE_SWITCH, INPUT_PULLUP);
      BtLEDOn = true;
      DebugPrint("BLE", "Starting BLE Client application...");
      BLEDevice::init("HRMClient");                                                                                     // Start BLE Client.
      BLEScan* pBLEScan = BLEDevice::getScan();                                                                         // Retrieve a Scanner.
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());                                        // Set the callback we want to use to be informed when wehave detected a new device.
      pBLEScan->setInterval(LOOP_DELAY/2);
      pBLEScan->setWindow(LOOP_DELAY/3);
      pBLEScan->setActiveScan(true);                                                                                    // Specify that we want active scanning.
    }

    void loop() {
      BlinkLED();
      if((gCurrentMillis - previousBLELoopMillis) >= LOOP_DELAY * 2) {                                                  // Only run the BLE loop as per previously set delay (x2).
        previousBLELoopMillis = gCurrentMillis;
        if(analogRead(MANUAL_OVERRIDE_SWITCH) != 0) {
          digitalWrite(OVERRIDE_RELAY, HIGH);
          if(gBLEScanInProgress) {
            DebugPrint("BLE", "Scanning stopped.");
            BLEDevice::getScan()->stop();
            gBLEScanInProgress = false;
          }
          if(gBLEConnected) {
            DebugPrint("BLE", "Disconnecting from HRM device.");
            pClientBLE->disconnect();
          }
          gBLEInitiateConnection = false;
          SetBLENotification = false;
        } else {
          digitalWrite(OVERRIDE_RELAY, LOW);
          if(gBLEInitiateConnection) {                                                                                  // If the flag "gBLEInitiateConnection" is true then we have scanned for and found the desired BLE HRM Device.
            if(connectToServer()) {                                                                                     // Now we connect to it. Once we are connected we set the gBLEConnected flag to be true.
              DebugPrint("BLE", "We are now connected to the BLE Server.");
            } else {
              DebugPrint("BLE", "We have failed to connect to the server; there is nothing more we will do.");
            }
            gBLEInitiateConnection = false;                                                                             // Reset the gBLEInitiateConnection to false.
          }
          if(gBLEConnected) {                                                                                           // If we are connected to a peer BLE Server, update the characteristic each time we are reached with the current time since boot.
            if(SetBLENotification) {
                DebugPrint("BLE", "Turning BLE Notification On.");
                const uint8_t onPacket[] = {0x01, 0x0};
                pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)onPacket, 2, true);
                SetBLENotification = false;
            }
          }
          if(!gBLEConnected && !gBLEInitiateConnection) {
            if(!gBLEScanInProgress) {
              DebugPrint("BLE", "Scannning for BLE HRM servers...");
            }
            BLEDevice::getScan()->start(LOOP_DELAY/1000, nullptr, false);                                               // Start the scan.
            gBLEScanInProgress = true;
          }
          ProcessHRZone();
        }
      }
    }
  };
cBLEClientService oBLEClientService;                                                                                     // BLE Object

void setup() {                                                                                                          // This is the Arduino setup function.
  Serial.begin(115200);
  WiFi.onEvent(oWiFiWebService.StationConnected, SYSTEM_EVENT_STA_CONNECTED);
  WiFi.onEvent(oWiFiWebService.StationGotIP, SYSTEM_EVENT_STA_GOT_IP);
  WiFi.onEvent(oWiFiWebService.StationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);
  WiFi.onEvent(oWiFiWebService.WiFiStationConnected, SYSTEM_EVENT_AP_STACONNECTED);
  WiFi.onEvent(oWiFiWebService.WiFiStationDisconnected, SYSTEM_EVENT_AP_STADISCONNECTED);
  oAccessEEPROM.ReadConfig();                                                                                           // Initialize EEPROM and read saved settings.
  oWiFiWebService.setup();                                                                                               // Run the WiFi Web Server setup function.
  oBLEClientService.setup();                                                                                             // Run the BLE HR Monitor setup function.
}                                                                                                                       // End of setup.

void loop() {                                                                                                           // This is the Arduino main loop function.
  gCurrentMillis = millis();                                                                                            // Capture the latest value of millis().
  oWiFiWebService.loop();                                                                                                // Run the WiFi Web Server main loop function.
  oBLEClientService.loop();                                                                                              // Run the BLE HR Monitor main loop function.  
}                                                                                                                       // End of main loop.

/*

Used Libs
  - WiFiManager           by tzapu           https://github.com/tzapu/WiFiManager
  - PubSubClient          by Nick O´Leary    https://github.com/knolleary/pubsubclient
  - ArduinoJson           by Benoit Blanchon https://github.com/bblanchon/ArduinoJson
  - ESPHTTPUpdateServerby by Tobias Faust    https://github.com/tobiasfaust/ESPHTTPUpdateServer
  - LITTLEFS              by lorol           https://github.com/lorol/LITTLEFS
  - Adafruit_GFX          by Adafruit        https://github.com/adafruit/Adafruit-GFX-Library
  - Adafruit_ST7789       by Adafruit        https://github.com/adafruit/Adafruit-ST7735-Library

To install the used libraries, use the embedded library manager (Sketch -> Include Library -> Manage Libraries),
or download them from github (Sketch -> Include Library -> Add .ZIP Library)

*/

#include <Arduino.h>
#include "config.h"

#include <ESPHTTPUpdateServer.h>


#if ENABLE_WEB_DEBUG == 1
char acWebDebug[1024] = "";
uint16_t u16WebMsgNo = 0;
#define WEB_DEBUG_PRINT(s) {if( (strlen(acWebDebug)+strlen(s)+50) < sizeof(acWebDebug) ) sprintf(acWebDebug, "%s#%i: %s\n", acWebDebug, u16WebMsgNo++, s);}
#else
#define WEB_DEBUG_PRINT(s) ;
#endif

// ---------------------------------------------------------------
// User configuration area end
// ---------------------------------------------------------------

#include "LittleFS.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Arduino_JSON.h>

#include <PubSubClient.h>

bool StartedConfigAfterBoot = false;
#define CONFIG_PORTAL_MAX_TIME_SECONDS 300
#include <WiFiManager.h>
#include "index.h"

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

#define BUTTON_TOP    35 // Active Lo
#define BUTTON_BOTTOM 0  // Active Lo; also used as GPIO0

#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC   16
#define TFT_BL    4 // Backlight

#define BG_COLOR ST77XX_BLACK

#define MQTT_TIMEOUT_THRESHOLD 60     // 60 seconds

#define FORMAT_LITTLEFS_IF_FAILED true


WiFiClient   espClient;

PubSubClient MqttClient(espClient);
WebServer httpServer(80);

ESPHTTPUpdateServer httpUpdater;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK); // ST7789 240x135

WiFiManager wm;
WiFiManagerParameter* custom_mqtt_server     = NULL;
WiFiManagerParameter* custom_mqtt_port       = NULL;
WiFiManagerParameter* custom_mqtt_topic      = NULL;
WiFiManagerParameter* custom_mqtt_json_entry = NULL;
WiFiManagerParameter* custom_mqtt_user       = NULL;
WiFiManagerParameter* custom_mqtt_pwd        = NULL;

const static char* serverfile    = "/mqtts";
const static char* portfile      = "/mqttp";
const static char* topicfile     = "/mqttt";
const static char* josnentryfile = "/mqttj";
const static char* userfile      = "/mqttu";
const static char* secretfile    = "/mqttw";

String mqttserver     = "";
String mqttport       = "";
String mqtttopic      = "";
String mqttjsonentry  = "";
String mqttuser       = "";
String mqttpwd        = "";

uint16_t u16MqttUpdateTimeout = 0xFFFF;
bool     bMqttTimeout = true;
long     previousConnectTryMillis = 0;
int32_t  i32ElectricalPower = 0;
byte     btnPressed = 0;
const char* update_path    = "/firmware";


bool bUserButtonPressed = false;

#include "images.h"

void   StartTrigger(void);
void   ResetTriggerOutputs(void);
String getId();

// -------------------------------------------------------
// Check the WiFi status and reconnect if necessary
// -------------------------------------------------------
void WiFi_Reconnect()
{
    uint16_t cnt = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
        wm.autoConnect();

        while (WiFi.status() != WL_CONNECTED)
        {
            delay(200);
            Serial.print("x");
        }

        Serial.println("");
        WiFi.printDiag(Serial);
        Serial.print("local IP:");
        Serial.println(WiFi.localIP());
        Serial.print("Hostname: ");
        Serial.println(HOSTNAME);

        WEB_DEBUG_PRINT("WiFi reconnected")
    }
}


void MqttSubCallback(char* topic, byte* payload, unsigned int length)
{
  JSONVar Obj;

  Obj = JSON.parse((const char*)payload);

  if (Obj.hasOwnProperty(mqttjsonentry.c_str())) 
  {
    u16MqttUpdateTimeout = 0;
    Serial.print(mqttjsonentry + ": ");
    i32ElectricalPower = Obj[mqttjsonentry.c_str()];
    Serial.println(i32ElectricalPower);
  }
  
}

// -------------------------------------------------------
// Check the Mqtt status and reconnect if necessary
// -------------------------------------------------------

bool MqttReconnect()
{
    if (mqttserver.length() == 0)
    {
        //No server configured
        return false;
    }

    if (WiFi.status() != WL_CONNECTED)
        return false;

    if (MqttClient.connected())
        return true;

    if (millis() - previousConnectTryMillis >= (5000))
    {

        Serial.print("MqttServer: ");      Serial.println(mqttserver);
        Serial.print("MqttUser: ");        Serial.println(mqttuser);
        Serial.print("MqttTopic: ");       Serial.println(mqtttopic);
        Serial.print("Mqtt Json Entry: "); Serial.println(mqttjsonentry);
        Serial.print("Attempting MQTT connection...");


        //Run only once every 5 seconds
        previousConnectTryMillis = millis();
        // Attempt to connect
        if (MqttClient.connect(getId().c_str(), mqttuser.c_str(), mqttpwd.c_str()) )
        {
            Serial.println("connected");

            // subscribe topic and callback which is called when /hello has come
            MqttClient.subscribe(mqtttopic.c_str());
            
            return true;
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(MqttClient.state());
            Serial.println(" try again in 5 seconds");
            WEB_DEBUG_PRINT("MQTT Connect failed")
            previousConnectTryMillis = millis();
        }
    }
    return false;
}

File this_file;

String load_from_file(const char* file_name, String defaultvalue) 
{
    String result = "";

    this_file = LittleFS.open(file_name, "r");
    if (!this_file) { // failed to open the file, return defaultvalue
        return defaultvalue;
    }

    while (this_file.available()) {
        result += (char)this_file.read();
    }

    this_file.close();
    return result;
}

bool write_to_file(const char* file_name, String contents) {
    File this_file = LittleFS.open(file_name, "w");
    if (!this_file) { // failed to open the file, return false
        return false;
    }

    int bytesWritten = this_file.print(contents);

    if (bytesWritten == 0) { // write failed
        return false;
    }

    this_file.close();
    return true;
}

void saveParamCallback()
{
    Serial.println("[CALLBACK] saveParamCallback fired");
    mqttserver = custom_mqtt_server->getValue();
    write_to_file(serverfile, mqttserver);

    mqttport = custom_mqtt_port->getValue();
    write_to_file(portfile, mqttport);

    mqtttopic = custom_mqtt_topic->getValue();
    write_to_file(topicfile, mqtttopic);

    mqttjsonentry = custom_mqtt_json_entry->getValue();
    write_to_file(josnentryfile, mqttjsonentry);

    mqttuser = custom_mqtt_user->getValue();
    write_to_file(userfile, mqttuser);

    mqttpwd = custom_mqtt_pwd->getValue();
    write_to_file(secretfile, mqttpwd);

    if (StartedConfigAfterBoot)
    {
        ESP.restart();
    }
}

String getId()
{
    uint64_t id = ESP.getEfuseMac();

    return String("PvExcess"+id);
}

void ConvertSecondsToHumanReadable(uint16_t Seconds, char *Str)
{
  uint8_t u8Min = Seconds/60;
  uint8_t u8Sec = Seconds%60;

  sprintf(Str, "%02u:%02u", u8Min, u8Sec);
}

void ConvertPowerToHumanReadable(int32_t i32Power, char *Str)
{
  float fPower = i32Power;
  
  if( abs(i32Power) >= 1000 )
    sprintf(Str, "%.1f kW", fPower/1000.0);
  else
    sprintf(Str, "%i W", i32Power);
}

void ExtendString(String *Str, uint8_t n)
{
  uint8_t u8Len = Str->length();
  if( u8Len < n )
  {
    u8Len = n - u8Len + 1;
    while(u8Len--)
      *Str += " ";
  }  
}

void DrawPower(int32_t Power)
{
  String sPow = String(abs(Power)) + " W";

  ExtendString(&sPow, 7);
  
  // Draw Arrow
  tft.drawFastHLine(95, 31, 90, ST77XX_WHITE);
  tft.setCursor(110, 50);
  tft.setTextColor(ST77XX_WHITE, BG_COLOR);
  tft.setTextSize(2);
  
  if( bMqttTimeout )
  {
    // Not connected to MQTT
    tft.print("      ");
  }
  else if( Power > 0 )
  {
    tft.drawLine(95, 31, 105, 21, ST77XX_WHITE);
    tft.drawLine(95, 31, 105, 41, ST77XX_WHITE);
    tft.drawLine(185, 31, 175, 21, BG_COLOR); // overwrite other arraw in bg color
    tft.drawLine(185, 31, 175, 41, BG_COLOR); // overwrite other arraw in bg color
    tft.print(sPow);
  }
  else
  {
    tft.drawLine(185, 31, 175, 21, ST77XX_WHITE);
    tft.drawLine(185, 31, 175, 41, ST77XX_WHITE);
    tft.drawLine(95, 31, 105, 21, BG_COLOR);  // overwrite other arraw in bg color
    tft.drawLine(95, 31, 105, 41, BG_COLOR);  // overwrite other arraw in bg color
    tft.print(sPow);
  }
}

#define MAX_NUM_CHARS_PER_LINE 20

void TftPrintStatus(String Line1, String Line2)
{
  ExtendString(&Line1, MAX_NUM_CHARS_PER_LINE);
  ExtendString(&Line2, MAX_NUM_CHARS_PER_LINE);

  tft.setTextColor(ST77XX_WHITE, BG_COLOR);
  tft.setTextSize(2);
  tft.setCursor(0, 100);
  tft.print(Line1);
  tft.setCursor(0, 120);
  tft.print(Line2);
}

uint16_t u16StartTimer = START_APPLIANCES_TIME;

typedef enum
{
  PVE_STATE_INIT,
  PVE_STATE_NO_WIFI,
  PVE_STATE_NO_MQTT,
  PVE_STATE_WAIT_FOR_USER,
  PVE_STATE_WAIT_FOR_EXCESS_POWER,
  PVE_STATE_WAIT_FOR_CONSTANT_EXCESS_POWER,
  PVE_STATE_RUNNING
}ePVE_STATE_t;

ePVE_STATE_t PveState;

// call every second
void DrawStartLogic(int32_t Power)
{
  char sTimer[10];
  char sPower[10];

  Power = Power * (-1); // Excess power is now positive

  ConvertSecondsToHumanReadable(u16StartTimer, sTimer);
  ConvertPowerToHumanReadable(EXCESS_POWER_TO_START, sPower);

  if( ((WiFi.status() != WL_CONNECTED) || (!MqttClient.connected())) && (PveState != PVE_STATE_NO_WIFI) && (PveState != PVE_STATE_NO_MQTT) )
    PveState = PVE_STATE_INIT;

  switch( PveState )
  {
    case PVE_STATE_INIT:
      PveState = PVE_STATE_NO_WIFI;
      break;
      
    case PVE_STATE_NO_WIFI:
      if( (WiFi.status() != WL_CONNECTED) )
      {
        u16StartTimer = START_APPLIANCES_TIME; // Reset start timer
        TftPrintStatus("Wifi not connected", "");
      }
      else
      {
        PveState = PVE_STATE_NO_MQTT;
      }
      break;
      
    case PVE_STATE_NO_MQTT:
      if( !MqttClient.connected() )
        TftPrintStatus("MQTT not connected", "");
      else
        PveState = PVE_STATE_WAIT_FOR_USER;
      break;
      
    case PVE_STATE_WAIT_FOR_USER:
      if( bUserButtonPressed )
      {
        PveState = PVE_STATE_WAIT_FOR_EXCESS_POWER;
      }
      else
        TftPrintStatus("", "Press start button");
      break;
      
    case PVE_STATE_WAIT_FOR_EXCESS_POWER:
      ResetTriggerOutputs();
      if( Power < EXCESS_POWER_TO_START )
      {
        u16StartTimer = START_APPLIANCES_TIME; // Not enough power, reset start timer
        TftPrintStatus("Not enough power", String(sPower) + " needed!");
      }
      else
        PveState = PVE_STATE_WAIT_FOR_CONSTANT_EXCESS_POWER;
      break;
      
    case PVE_STATE_WAIT_FOR_CONSTANT_EXCESS_POWER:
      TftPrintStatus("", "Starting in " + String(sTimer));

            // Excess power dropped below the threshold
      // Go one step back and wait for the sun to come out :-)
      if( Power < EXCESS_POWER_TO_START )
      {
        u16StartTimer = START_APPLIANCES_TIME; // Reset start timer
        PveState = PVE_STATE_WAIT_FOR_EXCESS_POWER;
      }
      else
      {
        if( u16StartTimer > 0)
          u16StartTimer--;
        else
        {
          StartTrigger();
          u16StartTimer = START_APPLIANCES_TIME; // Reset start timer
          PveState = PVE_STATE_RUNNING;
        }
      }
      break;
      
    case PVE_STATE_RUNNING:
      TftPrintStatus("Appliance started...", "Press for restart");
      if( bUserButtonPressed )
        PveState = PVE_STATE_WAIT_FOR_EXCESS_POWER;
      
      break;
  }

  bUserButtonPressed = false;
}

void StartTrigger(void)
{
  // Do whatever you want to start your appliances here
  digitalWrite(PERMANENT_OUTPUT, HIGH);
  digitalWrite(TRIGGER_OUTPUT,   HIGH);
  delay(500);
  digitalWrite(TRIGGER_OUTPUT,   LOW);
}

void ResetTriggerOutputs(void)
{
  digitalWrite(PERMANENT_OUTPUT, LOW);
  digitalWrite(TRIGGER_OUTPUT,   LOW);
  
  pinMode(PERMANENT_OUTPUT, OUTPUT); 
  pinMode(TRIGGER_OUTPUT,   OUTPUT);
}

void setup()
{
    ResetTriggerOutputs();
    
    pinMode(BUTTON_TOP,    INPUT); 
    pinMode(BUTTON_BOTTOM, INPUT);

    // turn on backlite
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init(135, 240); // Init ST7789 240x135
    tft.setRotation(3);
    tft.fillScreen(BG_COLOR);
    tft.drawBitmap(0, 0,      epd_bitmap_solar, 90, 62, ST77XX_WHITE, BG_COLOR);
    tft.drawBitmap(240-47, 0, epd_bitmap_grid,  47, 62, ST77XX_WHITE, BG_COLOR);
  
    Serial.begin(115200);
    Serial.println(F("Setup()"));
    WEB_DEBUG_PRINT("Setup()");

    LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);

    MqttClient.setCallback(MqttSubCallback);
  
    mqttserver     = load_from_file(serverfile,    "192.168.0.82");
    mqttport       = load_from_file(portfile,      "1883");
    mqtttopic      = load_from_file(topicfile,     "energy/solar");
    mqttjsonentry  = load_from_file(josnentryfile, "ElectricalPower");
    mqttuser       = load_from_file(userfile,      "");
    mqttpwd        = load_from_file(secretfile,    "");

    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

 
    // make sure the packet size is set correctly in the library
    MqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    
    custom_mqtt_server      = new WiFiManagerParameter("server",    "mqtt server",     mqttserver.c_str(),    40);
    custom_mqtt_port        = new WiFiManagerParameter("port",      "mqtt port",       mqttport.c_str(),      6);
    custom_mqtt_topic       = new WiFiManagerParameter("topic",     "mqtt topic",      mqtttopic.c_str(),     64);
    custom_mqtt_json_entry  = new WiFiManagerParameter("json_entry","mqtt json entry", mqttjsonentry.c_str(), 32);
    custom_mqtt_user        = new WiFiManagerParameter("username",  "mqtt username",   mqttuser.c_str(),      40);
    custom_mqtt_pwd         = new WiFiManagerParameter("password",  "mqtt password",   mqttpwd.c_str(),       40);
    
    wm.addParameter(custom_mqtt_server);
    wm.addParameter(custom_mqtt_port);
    wm.addParameter(custom_mqtt_topic);
    wm.addParameter(custom_mqtt_json_entry);
    wm.addParameter(custom_mqtt_user);
    wm.addParameter(custom_mqtt_pwd);
    wm.setSaveParamsCallback(saveParamCallback);
    
    std::vector<const char*> menu = { "wifi","wifinoscan","param","sep","erase","restart" };
    wm.setMenu(menu); // custom menu, pass vector

    // Set a timeout so the ESP doesn't hang waiting to be configured, for instance after a power failure
    wm.setConfigPortalTimeout(CONFIG_PORTAL_MAX_TIME_SECONDS);
    // Automatically connect using saved credentials,
    // if connection fails, it starts an access point with the specified name ("PvExcessConfig")
    bool res = wm.autoConnect(HOSTNAME, APPassword); // password protected wificonfig ap

    if (!res)
    {
        Serial.println(F("Failed to connect"));
        ESP.restart();
    }
    else
    {
        //if you get here you have connected to the WiFi
        Serial.println(F("connected...yeey :)"));
    }

    while (WiFi.status() != WL_CONNECTED)
    {
        WiFi_Reconnect();
    }


    uint16_t port = mqttport.toInt();
    if (port == 0)
        port = 1883;
    Serial.print(F("MqttServer: "));     Serial.println(mqttserver);
    Serial.print(F("MqttPort: "));       Serial.println(port);
    Serial.print(F("MqttTopic: "));      Serial.println(mqtttopic);
    Serial.print(F("MqttJsonEntry: "));  Serial.println(mqttjsonentry);
    MqttClient.setServer(mqttserver.c_str(), port);
 
    httpServer.on("/", MainPage);
    #if ENABLE_WEB_DEBUG == 1
        httpServer.on("/debug", SendDebug);
    #endif

    httpUpdater.setup(&httpServer, update_path, UPDATE_USER, UPDATE_PASSWORD);
    httpServer.begin();
}

#if ENABLE_WEB_DEBUG == 1
void SendDebug(void)
{
    httpServer.send(200, "text/plain", acWebDebug);
}
#endif

void MainPage(void)
{
    httpServer.send(200, "text/html", MAIN_page);
}

// -------------------------------------------------------
// Main loop
// -------------------------------------------------------
long ButtonTimer = 0;
long _1sTimer = 0;
bool bUserButtonOld = false;

void loop()
{
  bool bUserButton;

  bUserButton = (digitalRead(BUTTON_BOTTOM) == LOW);

  // Rising edge detector
  if( bUserButton && !bUserButtonOld)
  {
    bUserButtonPressed = true;
    Serial.println("User Button pressed");
    DrawStartLogic(i32ElectricalPower); // Redraw immediately for better user experience
  }

   bUserButtonOld = bUserButton;
  
    long now = millis();

    // every second
    if ((now - _1sTimer) > 1000)
    {
      _1sTimer = now;
      DrawPower(i32ElectricalPower);
      DrawStartLogic(i32ElectricalPower);
      if( u16MqttUpdateTimeout > MQTT_TIMEOUT_THRESHOLD )
      {
        i32ElectricalPower = 0;
        bMqttTimeout = true;  
      }
      else
      {
        u16MqttUpdateTimeout++;
        bMqttTimeout = false;
      }
    }

    if ((now - ButtonTimer) > BUTTON_TIMER)
    {
        ButtonTimer = now;

        if( AP_BUTTON_PRESSED )
        {
            if (btnPressed > 5)
            {
    
                Serial.println("Start AP");
                StartedConfigAfterBoot = true;
            }
            else
            {
                btnPressed++;
            }
            Serial.print("AP button pressed");
        }
        else
        {
            btnPressed = 0;
        }
    }

    if (StartedConfigAfterBoot == true)
    {
        TftPrintStatus("Starting AP", "IP: 192.168.4.1");
        httpServer.stop();
        Serial.println("Config after boot started");
        wm.setConfigPortalTimeout(CONFIG_PORTAL_MAX_TIME_SECONDS);
        wm.startConfigPortal(HOSTNAME, APPassword);
        delay(3000);
        ESP.restart();
    }

    WiFi_Reconnect();


    if (MqttReconnect())
    {
        MqttClient.loop();
    }


    httpServer.handleClient();
}

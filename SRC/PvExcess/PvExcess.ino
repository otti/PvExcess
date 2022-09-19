
/*

Used Libs
  - WiFiManager           by tzapu           https://github.com/tzapu/WiFiManager
  - PubSubClient          by Nick O´Leary    https://github.com/knolleary/pubsubclient
  - Arduino_Json          by Arduino         http://github.com/arduino-libraries/Arduino_JSON
  - ESPHTTPUpdateServerby by Tobias Faust    https://github.com/tobiasfaust/ESPHTTPUpdateServer
  - LITTLEFS              by lorol           https://github.com/lorol/LITTLEFS
  - Adafruit_GFX          by Adafruit        https://github.com/adafruit/Adafruit-GFX-Library
  - Adafruit_ST7789       by Adafruit        https://github.com/adafruit/Adafruit-ST7735-Library

To install the used libraries, use the embedded library manager (Sketch -> Include Library -> Manage Libraries),
or download them from github (Sketch -> Include Library -> Add .ZIP Library)

*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Arduino_JSON.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <ESPHTTPUpdateServer.h>
#include <SPI.h>
#include <Arduino_Json.h>

#include "LittleFS.h"
#include "config.h"
#include "config.html.h"
#include "index.html.h"
#include "images.h"

#define BUTTON_TOP     35 // Active Low; Located next to the reset button
#define BUTTON_BOTTOM  0  // Active Low; Also used as GPIO0


// SPI issue #1 (SPI speed)
// ------------------------
// The SPI clock is way to fast (26 MHz) and looks very ugly. It clocks between 1 and 3.3V and does not return back to 0 V.
// The ST7789V datasheet claims a "Serial clock cycle" of 66 ns (15,2 MHz)
// If we call "TftSpi.beginTransaction(SPISettings(15000000, MSBFIRST, SPI_MODE0));" the SPI stops sending clocks. Why?
// I even tried to write the ESP32 register directly. But it doesn't change the SPI speed at all.
uint32_t* VSPI_CLOCK_REG = (uint32_t*)0x3FF65018; // Pointer to ESP32 SPI clock register

// SPI issue #2 (MISO/MOSI)
// ------------------------
// The TTGO pinout says, that GPIO 19 is MOSI. But GPIO 19 shall be MISO regarding to the documentation.
// I have no clue why this is even working. We are using hardware SPI here.
// So how can this work with swapped MISO/MOSI Pins???

// ST7789V SPI
// ---------------------------------------------------------------------
// CS actvie LOW --> Idle Hi
// CLK Idle = LOW --> CPOL = 0
// Data changed on falling clock edge --> sampled on rising clock edge
// First bit sampled on first edge after CS --> CPHA = 0
// CPOL = 0 and CPHA = 0 --> SPI Mode 0
// Data is transmitted MSB first

#define TFT_MOSI       19 // SPI master out slave in (VSPID   Pin on ESP32; V for SPI #3; DSA Pin on ST7789V)
#define TFT_SCLK       18 // SPI clock               (VSPICLK Pin on ESP32; V for SPI #3; DCX Pin on ST7789V)
#define TFT_CS         5  // SPI chip select         (VSPICS  Pin on ESP32; V for SPI #3; CSX Pin on ST7789V)
#define TFT_DC         16 // Data/Command
#define TFT_RST        23 // Reset
#define TFT_BL         4  // Backlight

#define BG_COLOR       ST77XX_BLACK

#define MQTT_TIMEOUT_THRESHOLD         60     // 60 seconds

#define FORMAT_LITTLEFS_IF_FAILED      true

#define CONFIG_PORTAL_MAX_TIME_SECONDS 300

#if ENABLE_WEB_DEBUG == 1
char acWebDebug[1024] = "";
uint16_t u16WebMsgNo = 0;
#define WEB_DEBUG_PRINT(s) {if( (strlen(acWebDebug)+strlen(s)+50) < sizeof(acWebDebug) ) sprintf(acWebDebug, "%s#%i: %s\n", acWebDebug, u16WebMsgNo++, s);}
#else
#undef WEB_DEBUG_PRINT
#define WEB_DEBUG_PRINT(s) ;
#endif

#define DEFAULT_SETTINGS "{\"server\": \"192.168.0.82\", \"port\": \"1883\", \"user\": \"\", \"pass\": \"\", \"topic\": \"LS111/Metering\", \"key\": \"ElectricalPower\", \"power\" : \"2000\", \"time\": \"300\"}"
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

WiFiClient          espClient;
PubSubClient        MqttClient(espClient);
WebServer           httpServer(80);
ESPHTTPUpdateServer httpUpdater;

// Enable for software SPI. This is really slow
//Adafruit_ST7789     tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK); // ST7789 240x135

SPIClass TftSpi(VSPI); // TFT SPI on VSPI (SPI #3)
Adafruit_ST7789 tft = Adafruit_ST7789(&TftSpi, TFT_CS, TFT_DC, TFT_RST); // ST7789 240x135

WiFiManager         wm;

String sSettings;
JSONVar SettingsJson;

const static char* settingsfile    = "/settings";

uint16_t     u16MqttUpdateTimeout     = 0xFFFF;
int32_t      i32ElectricalPower       = 0;
bool         bMqttTimeout             = true;
long         previousConnectTryMillis = 0;
uint8_t      u8BtnPressCnt            = 0;
bool         StartedConfigAfterBoot   = false;
bool         bUserButtonPressed       = false;
const char*  update_path              = "/firmware";

uint16_t     u16StartTimer            = 300;
long         ButtonTimer              = 0;
long         _1sTimer                 = 0;
bool         bUserButtonOld           = false;
ePVE_STATE_t PveState;
File         this_file;

void   loop(void);
void   setup(void);
String getId(void);
void   MainPage(void);
void   StartTrigger(void);
bool   MqttReconnect(void);
void   WiFi_Reconnect(void);
//void   saveParamCallback(void);
void   DrawPower(int32_t Power);
void   ResetTriggerOutputs(void);
void   DrawStartLogic(int32_t Power);
void   ExtendString(String *Str, uint8_t n);
void   TftPrintStatus(String Line1, String Line2);
bool   write_to_file(const char* file_name, String contents);
void   ConvertPowerToHumanReadable(int32_t i32Power, char *Str);
void   ConvertSecondsToHumanReadable(uint16_t Seconds, char *Str);
String load_from_file(const char* file_name, String defaultvalue) ;
void   MqttSubCallback(char* topic, byte* payload, unsigned int length);

#if ENABLE_WEB_DEBUG == 1
void SendDebug(void)
#endif


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

        TftPrintStatus("", String("IP: ") + String(WiFi.localIP().toString()));
        delay(1500);

        WEB_DEBUG_PRINT("WiFi reconnected")
    }
}


// -------------------------------------------------------
// Will be called if an message with our topic has been received
// -------------------------------------------------------
void MqttSubCallback(char* topic, byte* payload, unsigned int length)
{
  JSONVar Obj;

  Obj = JSON.parse((const char*)payload);

  if (Obj.hasOwnProperty(SettingsJson["key"]))
  {
    u16MqttUpdateTimeout = 0;
    Serial.print(SettingsJson["key"]);
    Serial.print(": ");
    i32ElectricalPower = Obj[SettingsJson["key"]];
    Serial.println(i32ElectricalPower);
  }
  
}

// -------------------------------------------------------
// Check the Mqtt status and reconnect if necessary
// -------------------------------------------------------
bool MqttReconnect()
{
    if (SettingsJson["server"].length() == 0)
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
        Serial.print("Attempting MQTT connection...");

        //Run only once every 5 seconds
        previousConnectTryMillis = millis();
        // Attempt to connect
        if( MqttClient.connect(getId().c_str(), (const char*)SettingsJson["user"], (const char*)SettingsJson["pass"]) )
        {
            Serial.println("connected");

            // subscribe topic and callback which is called when /hello has come
            MqttClient.subscribe(SettingsJson["topic"]);
            
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

// add leading ' ' (space) to a string
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

// call every second
void DrawStartLogic(int32_t Power)
{
  char sTimer[10];
  char sPower[10];

  Power = Power * (-1); // Excess power is now positive

  ConvertSecondsToHumanReadable(u16StartTimer, sTimer);
  ConvertPowerToHumanReadable(atoi(SettingsJson["power"]), sPower);

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
        u16StartTimer =  atoi(SettingsJson["time"]); // Reset start timer
        TftPrintStatus("", "Wifi not connected");
      }
      else
      {
        PveState = PVE_STATE_NO_MQTT;
      }
      break;
      
    case PVE_STATE_NO_MQTT:
      if( !MqttClient.connected() )
        TftPrintStatus("", "MQTT not connected");
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
      if( Power < atoi(SettingsJson["power"]) )
      {
        u16StartTimer =  atoi(SettingsJson["time"]); // Not enough power, reset start timer
        TftPrintStatus("Not enough power", String(sPower) + " needed!");
      }
      else
        PveState = PVE_STATE_WAIT_FOR_CONSTANT_EXCESS_POWER;
      break;
      
    case PVE_STATE_WAIT_FOR_CONSTANT_EXCESS_POWER:
      TftPrintStatus("", "Starting in " + String(sTimer));

            // Excess power dropped below the threshold
      // Go one step back and wait for the sun to come out :-)
      if( Power < atoi(SettingsJson["power"]) )
      {
        u16StartTimer =  atoi(SettingsJson["time"]); // Reset start timer
        PveState = PVE_STATE_WAIT_FOR_EXCESS_POWER;
      }
      else
      {
        if( u16StartTimer > 0)
          u16StartTimer--;
        else
        {
          StartTrigger();
          u16StartTimer =  atoi(SettingsJson["time"]); // Reset start timer
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

    // Here again. I entered two times TFT_MOSI. Why is this even working???
    TftSpi.begin(TFT_SCLK, TFT_MOSI, TFT_MOSI, TFT_CS);

    // If we call this line to reduce the SPI speed, the SPI will stop sending clocks. Why?
    // TftSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

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
  
    sSettings = load_from_file(settingsfile,  DEFAULT_SETTINGS);
    SettingsJson = JSON.parse(sSettings);

    Serial.println("Current settings:");
    Serial.println(sSettings);

    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

    // make sure the packet size is set correctly in the library
    MqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);

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
        TftPrintStatus("", String("IP: ") + String(WiFi.localIP().toString()));
        delay(1500);
    }

    MqttClient.setServer(SettingsJson["server"], atoi(SettingsJson["port"]));
 
    httpServer.on("/", MainPage);
    httpServer.on("/config", ConfigPage);
    httpServer.on("/save_new_config_data", SaveConfigData);
    
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
    httpServer.send(200, "text/html", sMainPage);
}

void ConfigPage(void)
{
    String sJsonTxData =  "<script> var CurrentValues = '" + String(sSettings) + "'; </script>";
    Serial.print("Current settings: ");
    Serial.println(sSettings);
    httpServer.send(200, "text/html", sJsonTxData+ String(sConfigPage));
}

void SaveConfigData(void)
{
  JSONVar doc;

  for(int i=0; i<httpServer.args(); i++ )
    doc[httpServer.argName(i)] = httpServer.arg(i);

  String jsonString = JSON.stringify(doc);
  Serial.println(jsonString);

  write_to_file(settingsfile, jsonString);

  httpServer.send(200, "text/plain", "Data saved. Restarting device ....");
  delay(1000);
  ESP.restart();
}

// -------------------------------------------------------
// Main loop
// -------------------------------------------------------
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
            if (u8BtnPressCnt > 5)
            {
                Serial.println("Start AP");
                StartedConfigAfterBoot = true;
            }
            else
            {
                u8BtnPressCnt++;
            }
            Serial.print("AP button pressed");
        }
        else
        {
            u8BtnPressCnt = 0;
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
        MqttClient.loop();

    httpServer.handleClient();
}

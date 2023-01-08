#include <ESPmDNS.h>

#include <SPI.h>

#include <SPIFFS.h>

//#include <strings_en.h>
#include <WiFiManager.h>

#include <HTTP_Method.h>
#include <Uri.h>

//Read GPIO
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "Arduino.h"
#include <string.h>
#include "cjson/cJSON.h"
#include "time.h"
#include "espWEB.h"
#include "espWebErrorCodes.h"
#include "hueAccess.hpp"
#include "httpRequests.hpp"

#define BUTTON_PIN 18

#define ENC_A 17
#define ENC_B 19
#define ALARM_SWITCH_BUTTON 5
#define ROTARY_BUTTON 16
//#define BUTTON_TEST 16 // Wird nicht benötigt.
#define WIFI_RESET_PIN 18 // Auf einen der anderen Pins umlegen.
#define TIMEZONE "CET-1CEST,M3.5.0/02,M10.5.0/03"

#define JSON_CONFIG_FILE "/test_config.json"

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// Functions
void IRAM_ATTR rotaryEncoderTurnISR();
void IRAM_ATTR rotaryButtonISR();
void IRAM_ATTR rotaryButtonUPISR();
void IRAM_ATTR alarmButtonUp();
void IRAM_ATTR alarmButtonDown();
void IRAM_ATTR alarmButtonISR();
void saveConfigCallback();
void saveParamsCallback();

// Variables
WiFiManager wm;
TM1637Display * display2 = NULL; // Must be declared, so that other files can use it.
volatile int switchlightonce = 0;
const char* ntp_server = "de.pool.ntp.org";

struct tm currentTimeData;
struct tm wakeData;

volatile int alarmRotaryState = 0;
volatile int displayBrightness = 1;

volatile dimming_t *dimmingData;
volatile debouncing_t *debouncingData;

volatile int configuringHours = 0;
volatile int modifyAlarm = 0; // 0 = no, 1 = currently modifying, 2 = should be changed by Request
volatile int deleteScheduleNextLoop = 0;
volatile int downAlarmButtonTimeStamp;
volatile int upAlarmButtonTimeStamp;
bool shouldSaveConfig = false;
int errorState = 0;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;


// Tasks
void TaskTest(void *pvParameters){
  (void) pvParameters;
  int counter = 0;
  for(;;){
    Serial.print("TaskTest: ");
    Serial.println(counter);
    vTaskDelay(100 / portTICK_PERIOD_MS);  
  }
}

SemaphoreHandle_t displaySemaphore = NULL;
SemaphoreHandle_t blinkSemaphore = NULL;
SemaphoreHandle_t buttonSemaphore = NULL;

void TaskClockDisplay(void *pvParameters) {
  (void) pvParameters;
  for(;;){
    if(!getLocalTime(&currentTimeData)){
      Serial.println("Fehler beim Ermitteln der Zeit!");
      delay(1000);
    } else {
      if(!modifyAlarm){
        if(displayBrightness == 4){
          display2->setBrightness(displayBrightness, false); // displayBrightness = 8 --> AUS!
        } else {
          display2->setBrightness(displayBrightness);
        }
        display2->showNumberDecEx((currentTimeData.tm_hour)*100 + currentTimeData.tm_min, 0b01000000, true);
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if(modifyAlarm != 0){
      xSemaphoreGive(blinkSemaphore);
      if(xSemaphoreTake(displaySemaphore, portMAX_DELAY) == pdTRUE){
     
      } else {
        Serial.println("Error take display Semaphore");
      }
    }
  }
}


void TaskAlarmBlinkDisplay(void *pvParameters) {
  (void) pvParameters;
  for(;;){
    if(modifyAlarm == 1){
      for(int i = 0; i < 5; i++){
        display2->setBrightness(3);
        display2->showNumberDecEx(alarmRotaryState, 0b01000000, true);
        vTaskDelay(60 / portTICK_PERIOD_MS);
      }
      Serial.println(alarmRotaryState, DEC);
    
      for(int i = 0; i < 5; i++){
        display2->setBrightness(1);
        display2->showNumberDecEx(alarmRotaryState, 0b01000000, true);
        vTaskDelay(60 / portTICK_PERIOD_MS);
      }
    } else {
      
      if(xSemaphoreTake(blinkSemaphore, portMAX_DELAY) == pdTRUE){
        // Do  nothing, resume the loop
        xSemaphoreGive(displaySemaphore);
      } else {
        Serial.println("Error Take BlinkSemaphore");
      }
    }
  }
}

void TaskButtonHandler(void *pvParameters) {
  (void) pvParameters;
  for(;;){
    if(xSemaphoreTake(buttonSemaphore, portMAX_DELAY) == pdTRUE){
      if(deleteScheduleNextLoop){
        deleteRequest("testSchedule1");
        deleteScheduleNextLoop = 0;
      }
  
      if(modifyAlarm == 1){
        //updateAlarmDisplay();
      } else if(modifyAlarm == 2){
        updateRemoteAlarm();
      }
      
      if(switchlightonce){
        if(!switchLights())
          switchlightonce = 0;
      }

      if(dimmingData->dimValue != 0){
        dimLights();
        dimmingData->dimValue = 0;
        dimmingData->dimRotaryState = 0;
      }
    }
  }
}


void setup(void){
  /*
   * Größtenteils SPI/WiFi Init  
   */
  //bool forceConfig = false;
  display2 = new TM1637Display(CLK_PIN, DIO_PIN);

  dimmingData = new dimming_t; 
  debouncingData = new debouncing_t;
  
  totalApiKeyPath = (char *) malloc(sizeof(char)*100);
  apiKey = (char *) malloc(sizeof(char)*40);
  bridgeIP = (char *) malloc(sizeof(char)*40);
  controlledGroupName  = (char *) malloc(sizeof(char)*40);
  
  Serial.begin(115200);
  
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setAPCallback(configModeCallback);
  
  SPI.begin();
  
  InitializeFileSystem();
  bool spiffsSetup = loadConfigFile(); // Was wenn nicht möglich, weil Datei noch nicht existiert?
  
  pinMode(WIFI_RESET_PIN, INPUT);

  // check for forced reset (reset pin)
  checkForForcedReset();
  
  checkConnectionAndApiKey();
  
  if((!validBridgeIP) | (!apiKeyValid)){
    troubleshootConnection();
  }
 
  // Zeiteinstellung
  
  configureTime();
  
  // Display
  
  Serial.println("Set Display");
  display2->setBrightness(displayBrightness);

  delay(1000);
  
  // Inputs
  registerInputPeripherals();

  //xTaskCreatePinnedToCore(TaskTest, "TaskTest", 1024, NULL, 2, NULL, ARDUINO_RUNNING_CORE);

  displaySemaphore = xSemaphoreCreateBinary();
  blinkSemaphore   = xSemaphoreCreateBinary();
  buttonSemaphore  = xSemaphoreCreateBinary();
  
  xTaskCreatePinnedToCore(TaskAlarmBlinkDisplay, "TaskAlarmBlinkDisplay", 1024, NULL, 2, NULL, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(TaskClockDisplay, "TaskClockDisplay", 2048, NULL, 2, NULL, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(TaskButtonHandler, "TaskButtonHandler", 4096, NULL, 2, NULL, ARDUINO_RUNNING_CORE);
}

void loop() {
  Serial.println("Enter loop");
  
  delay(1000);
  if(!errorState){
    
  }else{
    display2->showNumberHexEx(errorState, 0, false, 4, 0);
  }
}

boolean InitializeFileSystem(){
  bool initok = false;
  initok = SPIFFS.begin();
  if(!(initok)){
    Serial.println("Format SPIFFS");
    SPIFFS.format();
    initok = SPIFFS.begin();  
  }
  if(!(initok)){ //try 2
    Serial.println("Try 2: Format SPIFFS");
    SPIFFS.format();
    initok = SPIFFS.begin();  
  }
  if(initok) {
    Serial.println("SPIFFS ok");
  }else{
    Serial.println("SPIFFS fehlerhaft");
  }
  return initok;
}

void saveConfigFile(){
  Serial.println("Saving Configuration...");

  //Create a JSON document
  JSONVar jsonDoc;
  jsonDoc["apiKey"] = apiKey;
  jsonDoc["bridgeIP"] = bridgeIP;
  jsonDoc["controlledGroupName"]  = controlledGroupName;
  jsonDoc["alarmTime"]   = alarmRotaryState;
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if(!configFile){
    Serial.println("Error opening JSON Config file");
  }
  configFile.close();
  //serializeJsonPretty(jsonDoc, Serial);
  Serial.println("stringified JSON: ");
  Serial.println(JSON.stringify(jsonDoc).c_str());
  if(jsonVarToSPIFFS(jsonDoc, JSON_CONFIG_FILE) == 0){
    Serial.println("Failed to write to file");
  } else {
    Serial.println("Successfully written to file");
  }


}

bool jsonVarToSPIFFS(JSONVar& json, char * storePath){
  File storage = SPIFFS.open(storePath, "wb");
  if(storage){
    Serial.println("Will write json to SPIFile");
    
    Serial.println(JSON.stringify(json).c_str());
    char * jsonBuffer = (char *) malloc(sizeof(char)*200); // Die größe dieser char * kann nicht durch sizeof bestimmt werden. ausser er ist nullterminiert dann strlen()
    strcpy(jsonBuffer, JSON.stringify(json).c_str());
    Serial.print("JsonBuffer: ");
    Serial.println(jsonBuffer);
    //size_t sizePayload = sizeof(jsonBuffer);
    Serial.print("Size of the stringified Json: ");
    Serial.println(sizeof(jsonBuffer));
    storage.write((uint8_t *) jsonBuffer, 200); // Vielleicht darf jsonBuffer so nicht übertragen werden?? nicht casten oder so?
    //storage.write(json, sizePayload);
  }
  storage.close();
  return true;
}

bool loadConfigFile()
// Load existing configuration file
{
  // Uncomment if we need to format filesystem
  // SPIFFS.format();
 
  // Read configuration from FS json
  Serial.println("Mounting File System...");
 
  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists(JSON_CONFIG_FILE))
    {
      // The file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
      if (configFile)
      {
        Serial.println("Opened configuration file");
        //StaticJsonDocument<512> json;
        Serial.print("Size of loaded config File: ");
        Serial.println(configFile.size());
        char * jsonStringBuffer = (char *) malloc(configFile.size());
        configFile.readBytes(jsonStringBuffer, configFile.size());
        
        Serial.println(jsonStringBuffer);
        
        JSONVar json = JSON.parse(jsonStringBuffer);
 
        //json
        
        //DeserializationError error = deserializeJson(json, configFile);
        Serial.println(JSON.stringify(json));
        //serializeJsonPretty(json, Serial);
        bool error = false;
        if (!error)
        {
          Serial.println("Parsing JSON");

          // Load Input Values          
          strcpy(apiKey, json["apiKey"]);
          strcpy(controlledGroupName, json["controlledGroupName"]);
          strcpy(bridgeIP, json["bridgeIP"]);
          if(json.hasOwnProperty("alarmTime")){
            alarmRotaryState = json["alarmTime"];
          }
          // construct Api path
          generateTotalApiKeyPath();
          return true;
        }
        else
        {
          // Error loading JSON data
          Serial.println("Failed to load json config");
        }
        configFile.close();
      }
    } else {
      strcpy(apiKey, "apiKey");
      strcpy(controlledGroupName, "controlledGroupName");
      strcpy(bridgeIP, "bridgeIP");
    }
  }
  else
  {
    // Error mounting file system
    Serial.println("Failed to mount FS");
  }
 
  return false;
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
  
  
}

void saveParamsCallback(){
  Serial.println("Get Params: ");
  if(wm.server->hasArg("config_selection")){ // Durch abc ersetzen?
    // specified in html name
    Serial.println("Server got arg selection");
    Serial.println(wm.server->arg("config_selection"));
  }
  //wm.shutdownConfigPortal();
  wm.stopConfigPortal();
}

void configModeCallback(WiFiManager *myWiFiManager)
// Called when config mode launched
{
  Serial.println("Entered Configuration Mode");
 
  Serial.print("Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
 
  Serial.print("Config IP Address: ");
  Serial.println(WiFi.softAPIP()); // ???
}
 

int calcOffset(int input){
  int t_count = 3;
  int t_input = input;
  while(t_input > 10){
    t_count -= 1;
    t_input = t_input/10;
  }
  return t_count;
}

//Das Licht selber kann nicht direkt hier geschaltet werden,
// weil alle Methoden, die im Interrupt ausgeführt werden das IRAM_ATTR Attribut benötigen.
void IRAM_ATTR buttonISR(){
  int timeStamp = millis();
  if(timeStamp > debouncingData->lastButtonDebounceTime + 500){
    if(modifyAlarm){
      modifyAlarm = 0;
    } else {
      switchlightonce = 1;
      Serial.println("ButtonTestISR");
    }
    debouncingData->lastButtonDebounceTime = millis();
    xSemaphoreGive(buttonSemaphore);
  }
}

volatile int rotaryButtonTS = 0;
void IRAM_ATTR rotaryButtonISR(){
  int timeStamp = millis();
  rotaryButtonTS = millis();
  if((timeStamp > debouncingData->lastButtonDebounceTime + 100) && (timeStamp > debouncingData->lastRotaryTimeStamp + 200)){
    
    debouncingData->lastButtonDebounceTime = millis();
    
    
    attachInterrupt(digitalPinToInterrupt(ROTARY_BUTTON), rotaryButtonUPISR, RISING);
  }
}

void IRAM_ATTR rotaryButtonUPISR(){
    int timeStamp = millis();
    if(timeStamp - rotaryButtonTS > 50){
      Serial.println("Rotary Button ISR");
      if(modifyAlarm != 0){
        if(configuringHours == 1){
          configuringHours = 0;
        } else {
          configuringHours = 1;
        }
      } else {
        displayBrightness = (displayBrightness + 1) % 5;
      }
      
    }
    debouncingData->lastButtonDebounceTime = millis();
    attachInterrupt(digitalPinToInterrupt(ROTARY_BUTTON), rotaryButtonISR, FALLING);
    //registerButton(ROTARY_BUTTON,FALLING, rotaryButtonISR, INPUT);
}

void IRAM_ATTR alarmButtonISR(){
  if(digitalRead(ALARM_SWITCH_BUTTON) == HIGH){
    //Serial.print("D");
    debouncingData->lastButtonDebounceTimeUp = millis();
    downAlarmButtonTimeStamp = millis();
  } else {
    upAlarmButtonTimeStamp = millis();
    if((upAlarmButtonTimeStamp > debouncingData->lastButtonDebounceTimeDown + 500) & (upAlarmButtonTimeStamp > debouncingData->lastButtonDebounceTimeUp)){
      //Serial.println("AlarmButton");
      switch(modifyAlarm){
        case 0: modifyAlarm = 1; break;
        case 1: modifyAlarm = 2; break;
        case 2: modifyAlarm = 2; break;
        default: break;
      }
      if(((upAlarmButtonTimeStamp - downAlarmButtonTimeStamp) >= 3000) && ((upAlarmButtonTimeStamp - downAlarmButtonTimeStamp) <= 10000)){ //Long Press
        //Serial.print("UpTimestamp: ");
        //Serial.println(upAlarmButtonTimeStamp);
        //Serial.print("DownTimestamp: ");
        //Serial.println(downAlarmButtonTimeStamp);
        //Delete Request!
        //Serial.println("Delete next loop");
        deleteScheduleNextLoop = 1;
        modifyAlarm = 0;
        
      }
      
      
      debouncingData->lastButtonDebounceTimeDown = millis();
      xSemaphoreGiveFromISR(buttonSemaphore, NULL);
    } 
  }
}

void IRAM_ATTR buttonTest(){
  int timeStamp = millis();
  if(timeStamp > debouncingData->lastButtonDebounceTime + 500){
    Serial.println("ButtonTest!");
    Serial.println(timeStamp);
    switchlightonce = 1;
    debouncingData->lastButtonDebounceTime = millis();
  }
}

void c_remove_squarebrackets(char * input, char * output){
    while(*input){
      if((*input != '[') && (*input != ']')){
        *output++ = *input;
      }
      input++;
    }
    *output = '\0';
}



void checkForForcedReset(){
  int resetpinval = digitalRead(WIFI_RESET_PIN);
  if(resetpinval){
    WiFiManagerParameter philipsAPIKey("HueAPIKey", "API-Key", apiKey, 40);
    WiFiManagerParameter hueBridgeIP("BridgeIP", "Bridge-IP", bridgeIP, 40);
    WiFiManagerParameter groupName("GroupName", "Group Name", controlledGroupName, 40);
    Serial.println("add config field");
    WiFiManagerParameter customText("<p>Only change Bridge IP/HueAPIKey, if autodetection/setup fails multiple times (E2/E3 code).</p>");
    //WiFiManagerParameter config_field(config_str);
    Serial.println("Starte Accesspoint!");

    wm.addParameter(&customText);
    wm.addParameter(&philipsAPIKey); // nur bei Steuerung über WifiManager
    wm.addParameter(&hueBridgeIP); 
    wm.addParameter(&groupName);
    //wm.addParameter(&config_field);
    wm.setCaptivePortalEnable(true);
    wm.startConfigPortal("OnDemandAP");
    JSONVar testVar;
    
    strcpy(apiKey, philipsAPIKey.getValue());
    strcpy(bridgeIP, hueBridgeIP.getValue());
    strcpy(controlledGroupName, groupName.getValue());
    
    testVar["apiKey"] = apiKey; 
    testVar["bridgeIP"] = bridgeIP;
    testVar["controlledGroupName"] = controlledGroupName;
  
    jsonVarToSPIFFS(testVar, JSON_CONFIG_FILE);

    generateTotalApiKeyPath();
    
    if(wm.autoConnect("AutoConnectAP", "password")){
      Serial.println("successfully connected");
    } else {
      Serial.println("failed to connect");
    }
    
    Serial.print("Api Path: ");
    Serial.println(totalApiKeyPath);
    Serial.println("connected!");
  } else {
    bool res;

    res = wm.autoConnect("AutoConnectAP", "password");
  
    if(!res) {
      Serial.println("Failed to connect!");
    }else{
      Serial.println("connected");
    }
  }
}


void configureTime(){
  Serial.println("Config Time");
  configTime(0, 0, ntp_server); // stammt wohl aus der esp32-hal-time.c
  // vor Trennung des WLANs einmal getLocalTime()
  // aufrufen; sonst wird die Zeit nicht übernommen
  Serial.println("Get Time");
  getLocalTime(&currentTimeData);
  getLocalTime(&wakeData);
  setenv("TZ", TIMEZONE, 1);  // Zeitzone einstellen
}

void registerInputPeripherals(){
  Serial.println("Register Buttons");
  registerButton(BUTTON_PIN, RISING, buttonISR, INPUT_PULLDOWN);
  Serial.println("Registered Button_Pin");
  registerButton(ROTARY_BUTTON,FALLING, rotaryButtonISR, INPUT);
  Serial.println("Registered Rotary BUtton");
  registerButton(ALARM_SWITCH_BUTTON, CHANGE, alarmButtonISR, INPUT_PULLDOWN);
  //registerButton(ALARM_SWITCH_BUTTON, RISING, alarmButtonDown, INPUT_PULLDOWN);
  Serial.println("Registered Alarm Button");
  initRotaryEncoder(ENC_B, ENC_A, rotaryEncoderTurnISR, CHANGE);
  Serial.println("Registered Encoders");
  //registerButton(BUTTON_TEST, FALLING, buttonTest, INPUT_PULLDOWN);
  //Serial.println("Registered TestButton");
  Serial.println("Register Buttons done");
}

void updateAlarmDisplay(){
  display2->setBrightness(3);
  display2->showNumberDecEx(alarmRotaryState, 0b01000000, true);
  
  Serial.println(alarmRotaryState, DEC);
  
  delay(800);
  display2->setBrightness(1);
  display2->showNumberDecEx(alarmRotaryState, 0b01000000, true);
}

void updateRemoteAlarm(){
  //Update Alarm
  char * timeString = (char *) malloc(sizeof(char)*21);
  getLocalTime(&wakeData);
  if( (alarmRotaryState/100 < 24) & (alarmRotaryState/100 >= 0) & (alarmRotaryState%100 < 60) & (alarmRotaryState%100 >= 0)){
    wakeData.tm_hour = alarmRotaryState/100;
    wakeData.tm_min = alarmRotaryState%100;
    time_t wakeTime = mktime(&wakeData);
    time_t currTime = mktime(&currentTimeData);
    if(difftime(wakeTime, currTime) < 0){
     wakeData.tm_mday += 1;
    }
    snprintf(timeString, 21, "%d-%02d-%02dT%02d:%02d:00", wakeData.tm_year+1900,wakeData.tm_mon+1, wakeData.tm_mday, alarmRotaryState/100, alarmRotaryState%100);
    updateLightSchedule("testSchedule1", true, timeString, controlledGroupName);
    saveConfigFile();
    modifyAlarm = 0;
  } else {
    Serial.println("Invalid Rotary State!");
    modifyAlarm = 1;
  }
  Serial.println(timeString);
}

int mod(int a, int b)
{
   if(b < 0) //you can check for b == 0 separately and do what you want
     return -mod(-a, -b);   
   int ret = a % b;
   if(ret < 0)
     ret+=b;
   return ret;
}

/*
void IRAM_ATTR dimLightInterrupt(){
  dimmingData->lastBrightnessRotaryValue = dimmingData->dimRotaryState;
  timerAlarmDisable(timer);
  timerStop(timer);
  dimmingData->determiningDimFactor = false;
  dimmingData->dimValue = dimmingData->lastBrightnessRotaryValue - dimmingData->firstBrightnessRotaryValue;
  dimmingData->dimValue = constrain(dimmingData->dimValue, -255, 255);
  xSemaphoreGiveFromISR(buttonSemaphore, NULL);
  Serial.println("dimInterrupt");
}
*/
void IRAM_ATTR rotaryEncoderTurnISR()
{
  
  static uint8_t old_AB = 3;  // Lookup table index
  static int8_t encval = 0;   // Encoder value  
  static const int8_t enc_states[]  = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0}; // Lookup table

  old_AB <<=2;  // Remember previous state

  if (digitalRead(ENC_A)) old_AB |= 0x02; // Add current state of pin A
  if (digitalRead(ENC_B)) old_AB |= 0x01; // Add current state of pin B
  
  encval += enc_states[( old_AB & 0x0f )];

  // Update counter if encoder has rotated a full indent, that is at least 4 steps
  if( encval > 3 ) {        // Four steps forward
    if(modifyAlarm){
      if(configuringHours){
        alarmRotaryState = (alarmRotaryState + 100) % 2400;
      } else {
        alarmRotaryState++;              // Increase counter
        if(alarmRotaryState % 100 == 60){
          alarmRotaryState = alarmRotaryState + 40;
          if(alarmRotaryState >= 2400) {
            alarmRotaryState = alarmRotaryState % 100;
          }
        } else {
          alarmRotaryState = (alarmRotaryState/100)*100 + (alarmRotaryState%100)%60;
        }
      }
    }else{
      dimmingData->dimRotaryState = dimmingData->dimRotaryState + 15;  
    }
    encval = 0;
  }
  else if( encval < -3 ) {  // Four steps backwards
    if(modifyAlarm){
       if(configuringHours){
         alarmRotaryState = mod(alarmRotaryState - 100, 2400); 
       }else{
         if((alarmRotaryState % 100 == 0) && alarmRotaryState != 0){
          alarmRotaryState = alarmRotaryState - 41;
         } else if(alarmRotaryState == 0){
          alarmRotaryState = 2359;
         } else {
          alarmRotaryState--;
         }
         //alarmRotaryState = (alarmRotaryState/100)*100 + mod((alarmRotaryState%100),60);
       }                  // Decrease counter
    } else {
      dimmingData->dimRotaryState = dimmingData->dimRotaryState - 15;
    }
    encval = 0;
  }

  debouncingData->lastRotaryTimeStamp = millis();
  
  if(!modifyAlarm){
    if (!dimmingData->determiningDimFactor){
      //timer = timerBegin(0, 80, true);
      //timerAttachInterrupt(timer, &dimLightInterrupt, true);
      //timerAlarmWrite(timer, 1000000, true);
      //timerAlarmEnable(timer);
      dimmingData->dimValue = dimmingData->dimRotaryState;
      dimmingData->dimValue = constrain(dimmingData->dimValue, -255, 255);
      xSemaphoreGiveFromISR(buttonSemaphore, NULL);
    } else {
      //timerRestart(timer);
      
    }
  }
}


/*
 *  initialize rotary encoder for alarm settings
 */
void initRotaryEncoder(int pinA, int pinB, void (*targetFunctionA)(void), int event){
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinA), targetFunctionA, event);
  attachInterrupt(digitalPinToInterrupt(pinB), targetFunctionA, event);
}

int dimLights(){
  if(WiFi.status()== WL_CONNECTED){
    
    String httpRequestData;
    HTTPClient http;

    /*
     * Create Post Request Body
     */
    
    httpRequestData = String("{ \"bri_inc\":" + String(dimmingData->dimValue) + "}");
    
    int id = getGroupID(controlledGroupName);
    
    String serverPath = String(totalApiKeyPath) +"groups/" + String(id) + "/action";
    
    //Serial.print("ID: ");
    //Serial.println(id, DEC);
    //Serial.print("Pfad: ");
    //Serial.println(serverPath);
    //Serial.print("Wert: ");
    //Serial.println(httpRequestData);
    
    http.begin(serverPath.c_str());
    int httpResponseCode = http.PUT(httpRequestData); //Do actual Request to API

    if (httpResponseCode>0) {
      String payload = http.getString();

      //Serial.print("HTTP Response code: ");
      //Serial.println(httpResponseCode);
      //Serial.println(payload);
      return 0;
    }
    else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      return 1;
    }
    
    http.end();
    dimmingData->dimValue = 0;
  }
  else {
    Serial.println("WiFi Disconnected");
  }
}

/*
 * switch status of Lights
 */
int switchLights(){
  
  if(WiFi.status()== WL_CONNECTED){
    
    String httpRequestData;
    HTTPClient http;

    /*
     * Create Post Request Body
     */
    if(getGroupStatus(controlledGroupName)){
      httpRequestData = "{ \"on\": false}";
    } else {
      httpRequestData = "{ \"on\": true}";
    }
    
    int id = getGroupID(controlledGroupName);
    
    String serverPath = String(totalApiKeyPath) +"groups/" + String(id) + "/action";
    
    Serial.print("ID: ");
    Serial.println(id, DEC);
    Serial.print("Pfad: ");
    Serial.println(serverPath);
    Serial.print("Wert: ");
    Serial.println(httpRequestData);
    
    http.begin(serverPath.c_str());
    int httpResponseCode = http.PUT(httpRequestData); //Do actual Request to API

    if (httpResponseCode>0) {
      String payload = http.getString();

      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println(payload);
      return 0;
    }
    else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      return 1;
    }
    
    http.end();
  }
  else {
    Serial.println("WiFi Disconnected");
  }
  
}


/*
 * param
 * pin: GPIO number
 * event: RISING, FALLING, HIGH, LOW, CHANGE
 * targetFunction: ISR function which should be executed.
 * targetFunction must have the signature: void IRAM_ATTR functionname(){...}
 * modus = INPUT,....
 */
int registerButton(uint8_t pin, int event, void (*targetFunction)(void), int modus){
  //Serial.println("Register Button");
  pinMode(pin, modus);
  //Serial.println("Attach Interrupt");
  attachInterrupt(digitalPinToInterrupt(pin), targetFunction, event);
  //Serial.println("Interrupt attached");
  return 0;
}
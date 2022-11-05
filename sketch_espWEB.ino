#include <ESPmDNS.h>

#include <SPI.h>

#include <SPIFFS.h>

//#include <strings_en.h>
#include <WiFiManager.h>

#include <HTTP_Method.h>
#include <Uri.h>


#include <TM1637Display.h>

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

#define INPUT_PIN 1
#define MAX_DEVICE_GROUPS 256
#define BUTTON_PIN 26
#define CLK_PIN 33
#define DIO_PIN 32
#define ENC_A 13
#define ENC_B 14
#define ALARM_SWITCH_BUTTON 17
#define ROTARY_BUTTON 27
#define BUTTON_TEST 16
#define WIFI_RESET_PIN 4

#define JSON_CONFIG_FILE "/test_config.json"

TM1637Display display2(CLK_PIN, DIO_PIN);

WiFiManager wm;

char * totalApiKeyPath;
char * apiKey;
char * bridgeIP;
char * controlledGroupName;  // -> Group to control
volatile int switchlightonce = 0;
const char* ntp_server = "de.pool.ntp.org";
int errorState = 0;

#define TIMEZONE "CET-1CEST,M3.5.0/02,M10.5.0/03"

struct tm currentTimeData;
struct tm wakeData;
volatile int rotaryState = 0;
volatile int lastButtonDebounceTime = 0;
volatile int lastButtonDebounceTimeDown = 0;
volatile int lastButtonDebounceTimeUp = 0;

void IRAM_ATTR rotaryEncoderTurnISR();
void IRAM_ATTR rotaryButtonISR();
void IRAM_ATTR alarmButtonUp();
void IRAM_ATTR alarmButtonDown();

void saveConfigCallback();
void saveParamsCallback();

volatile int configuringHours = 0;
volatile int modifyAlarm = 0; // 0 = no, 1 = currently modifying, 2 = should be changed by Request
volatile int deleteScheduleNextLoop = 0;
volatile int downAlarmButtonTimeStamp;
volatile int upAlarmButtonTimeStamp;
bool shouldSaveConfig = false;
bool validBridgeIP = false;
bool apiKeyValid = false;

// Not needed, API-Key and Bridge IP are automatically put in for the latest values, just dont change them
/* const char * config_str = "<p> Overwrite API-Key and Bridge-IP Config? </p>" \
                          "<input style='width: auto; margin: 0 10px 0 10px;' type='radio' id='choice1' name='config_selection' value='STD' checked='checked' > <label for='choice1'> Standard </label><br>" \
                          "<input style='width: auto; margin: 0 10px 0 10px;' type='radio' id='choice2' name='config_selection' value='ALT1'> <label for='choice2'> Alternative 1 </label><br>" \
                          "<input style='width: auto; margin: 0 10px 0 10px;' type='radio' id='choice3' name='config_selection' value='ALT2'> <label for='choice3'> Alternative 2 </label><br>" \
                          "<input style='width: auto; margin: 0 10px 0 10px;' type='radio' id='choice4' name='config_selection' value='ALT3'> <label for='choice4'> Alternative 3 </label><br>"; */

void setup(void){
  /*
   * Größtenteils SPI/WiFi Init  
   */
  //bool forceConfig = false;
  
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
  
  /*
   * Ab hier auch der Rest (die alte setupFunktion);
   */
  
  // Zeiteinstellung
  
  configureTime();
  
  // Display
  
  Serial.println("Set Display");
  display2.setBrightness(2);

  delay(1000);
  
  // Inputs
  registerInputPeripherals();
  
}

void loop() {
  Serial.println("Enter loop");
  
  delay(1000);
  if(!errorState){
    if(!getLocalTime(&currentTimeData)){
      Serial.println("Fehler beim Ermitteln der Zeit!");
      delay(1000);
    } else {
      if(!modifyAlarm){
        display2.showNumberDecEx((currentTimeData.tm_hour)*100 + currentTimeData.tm_min, 0b01000000, true);
      }
    }
    
    if(deleteScheduleNextLoop){
      deleteRequest("testSchedule1");
      deleteScheduleNextLoop = 0;
    }

    
    if(modifyAlarm == 1){
      updateAlarmDisplay();
    } else if(modifyAlarm == 2){
      updateRemoteAlarm();
    }
    
    delay(1000);
    
    if(switchlightonce){
      if(!switchLights())
        switchlightonce = 0;
    }
  }else{
    display2.showNumberHexEx(errorState, 0, false, 4, 0);
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
  char testString[50] = "test value";
  int testNumber = 1234;
  jsonDoc["testString"] = testString;
  jsonDoc["testNumber"] = testNumber;
  jsonDoc["apiKey"] = apiKey;
  jsonDoc["bridgeIP"] = bridgeIP;
  jsonDoc["controlledGroupName"]  = controlledGroupName;
  jsonDoc["alarmTime"]   = rotaryState;
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
            rotaryState = json["alarmTime"];
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
  if(timeStamp > lastButtonDebounceTime + 500){
    if(modifyAlarm){
      modifyAlarm = 0;
    } else {
      switchlightonce = 1;
      Serial.println("ButtonTestISR");
    }
    lastButtonDebounceTime = millis();
  }
}

void IRAM_ATTR rotaryButtonISR(){
  int timeStamp = millis();
  if(timeStamp > lastButtonDebounceTime + 500){
    if(configuringHours == 1){
      configuringHours = 0;
    } else {
      configuringHours = 1;
    }
    lastButtonDebounceTime = millis();
  }
}

void IRAM_ATTR alarmButtonDown(){
  int timeStamp = millis();
  if(timeStamp > lastButtonDebounceTimeUp + 500){
    lastButtonDebounceTimeUp = millis();
    downAlarmButtonTimeStamp = millis();
    attachInterrupt(digitalPinToInterrupt(ALARM_SWITCH_BUTTON), alarmButtonUp, FALLING);
  }
}

void IRAM_ATTR alarmButtonUp(){
  upAlarmButtonTimeStamp = millis();
  if(upAlarmButtonTimeStamp > lastButtonDebounceTimeDown + 500){
    Serial.println("AlarmButton");
    switch(modifyAlarm){
      case 0: modifyAlarm = 1; break;
      case 1: modifyAlarm = 2; break;
      case 2: modifyAlarm = 2; break;
      default: break;
    }
    if((upAlarmButtonTimeStamp - downAlarmButtonTimeStamp) >= 3000){ //Long Press
      //Delete Request!
      deleteScheduleNextLoop = 1;
      modifyAlarm = 0;
    }
    attachInterrupt(digitalPinToInterrupt(ALARM_SWITCH_BUTTON), alarmButtonDown, RISING);
    lastButtonDebounceTimeDown = millis();
    
  }
}

void IRAM_ATTR buttonTest(){
  int timeStamp = millis();
  if(timeStamp > lastButtonDebounceTime + 500){
    Serial.println("ButtonTest!");
    Serial.println(timeStamp);
    switchlightonce = 1;
    lastButtonDebounceTime = millis();
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

void generateTotalApiKeyPath(){
  strcpy(totalApiKeyPath, "http://");
  strcat(totalApiKeyPath, bridgeIP);
  strcat(totalApiKeyPath, "/api/");
  strcat(totalApiKeyPath, apiKey);
  strcat(totalApiKeyPath, "/");
  Serial.print("generated API Key Path: ");
  Serial.println(totalApiKeyPath);
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

void troubleshootConnection() {
  if(!validBridgeIP){
      Serial.println("Bridge not connected, try new connection");
      getIpViaMDNS(); // Connect to Bridge (-> update IP adress, if changed)

      // again check the connection, IP/hostname should now be correct, check for API Key
      checkConnectionAndApiKey();
      if(!validBridgeIP){
        Serial.println("Bridge still not connected! There might be an error in your configuration.");
        display2.showNumberHexEx(err_bridgeIP, 0, false, 4, 0);
        errorState = err_bridgeIP;
      } else {
        if(!apiKeyValid){
          Serial.println("API-Key invalid, check again");
          checkConnectionAndApiKey();
          
          if(!apiKeyValid){
            Serial.println("API-Key still invalid, will try to get new on");
            getAndSaveNewAPIKey(10);  
          }
          if(!apiKeyValid){
            Serial.println("API-Key still invalid, personal inspection needed");
            display2.showNumberHexEx(err_apiKey, 0, false, 4, 0);
            errorState = err_apiKey;
          }
        }
      }
      generateTotalApiKeyPath();
      
      saveConfigFile();
  } else {
    if(!apiKeyValid){
      // get new API Key -> indicate via some light? 
      Serial.println("API-Key invalid, check again");
      checkConnectionAndApiKey();
      if(!apiKeyValid){
        display2.showNumberHexEx(err_apiKey, 0, false, 4, 0);
        Serial.println("API-Key still invalid, will try to get new on");
        getAndSaveNewAPIKey(10);
      }
      checkConnectionAndApiKey();
      if(!apiKeyValid){
        Serial.println("API-Key still invalid, personal inspection needed");
        display2.showNumberHexEx(err_apiKey, 0, false, 4, 0);
        errorState = err_apiKey;
      } else {
        generateTotalApiKeyPath();
        saveConfigFile();
      }
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
  registerButton(BUTTON_PIN, FALLING, buttonISR, INPUT);
  Serial.println("Registered Button_Pin");
  registerButton(ROTARY_BUTTON,FALLING, rotaryButtonISR, INPUT_PULLUP);
  Serial.println("Registered Rotary BUtton");
  registerButton(ALARM_SWITCH_BUTTON, RISING, alarmButtonDown, INPUT);
  Serial.println("Registered Alarm Button");
  initRotaryEncoder(ENC_B, ENC_A, rotaryEncoderTurnISR, CHANGE);
  Serial.println("Registered Encoders");
  registerButton(BUTTON_TEST, FALLING, buttonTest, INPUT);
  Serial.println("Registered TestButton");
  Serial.println("Register Buttons done");
}

void updateAlarmDisplay(){
  display2.setBrightness(3);
  display2.showNumberDecEx(rotaryState, 0b01000000, true);
  
  Serial.println(rotaryState, DEC);
  
  delay(800);
  display2.setBrightness(1);
  display2.showNumberDecEx(rotaryState, 0b01000000, true);
}

void updateRemoteAlarm(){
  //Update Alarm
  char * timeString = (char *) malloc(sizeof(char)*21);
  getLocalTime(&wakeData);
  if( (rotaryState/100 < 24) & (rotaryState/100 >= 0) & (rotaryState%100 < 60) & (rotaryState%100 >= 0)){
    wakeData.tm_hour = rotaryState/100;
    wakeData.tm_min = rotaryState%100;
    time_t wakeTime = mktime(&wakeData);
    time_t currTime = mktime(&currentTimeData);
    if(difftime(wakeTime, currTime) < 0){
     wakeData.tm_mday += 1;
    }
    snprintf(timeString, 21, "%d-%02d-%02dT%02d:%02d:00", wakeData.tm_year+1900,wakeData.tm_mon+1, wakeData.tm_mday, rotaryState/100, rotaryState%100);
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
    if(configuringHours){
      rotaryState = rotaryState + 100;
    } else {
      rotaryState++;              // Increase counter
      rotaryState = (rotaryState/100)*100 + (rotaryState%100)%60;
    }
    encval = 0;
  }
  else if( encval < -3 ) {  // Four steps backwards
   if(configuringHours){
     rotaryState = rotaryState - 100; 
   }else{
     rotaryState--;
     rotaryState = (rotaryState/100)*100 + mod((rotaryState%100),60);
   }                  // Decrease counter
   encval = 0;
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

/*
 * Configure new API Key
 * timeout: how often shall getting a new Key be tried
 * will try to get a key all 15 seconds, as long as timeout isnt reached
 */
void getAndSaveNewAPIKey(int timeout){
  bool configured = false;
  for(int i = 0; i < timeout; i++){
    JSONVar inVar;
    
    char * t_path = (char *) malloc(sizeof(char)*50);
    strcpy(t_path, "http://");
    strcat(t_path, bridgeIP);
    strcat(t_path, "/api/");
    char * apiRequest = "{\"devicetype\":\"clockApp\"}";
    postRequest(t_path, apiRequest, inVar);
    if(inVar.hasOwnProperty("error")){
      Serial.println(JSON.stringify(inVar));
      Serial.println("Might try again in a few seconds (15), please press link button, if error tells so");
      delay(15000);
    } else {
      if(inVar.hasOwnProperty("success")){
        strcpy(apiKey, inVar["success"]["username"]);
        Serial.print("New API Key generated: ");
        Serial.println(inVar["success"]["username"]);
        configured = true;
      }
    }
    
    free(t_path);
    if(configured) {
      break;
    }
  }
  if(!configured){
    Serial.println("configuration of new API-Key failed, please try again later.");
  }
}

/*
 * Connects to HUE Bridge via mDNS in local network
 * API Key an anderer Stelle erneuern.
 */
void getIpViaMDNS(){
  if(WiFi.status() == WL_CONNECTED){
    JSONVar inVar; //= (JSONVar *) malloc(sizeof(JSONVar));
    if(!MDNS.begin("ESP32_Browser")){
      Serial.println("Error setting up MDNS responder");
    }
    int n = MDNS.queryService("hue", "tcp");
    if(n == 0){
      Serial.println("No hue service found");
    } else {
      Serial.print(n);
      Serial.println(" Service(s) found");
      for(int i = 0; i < n; i++){
        Serial.print(" ");
        Serial.print(i+1);
        Serial.print(": ");
        Serial.print(MDNS.hostname(i));
        Serial.print(" (");
        Serial.print(MDNS.IP(i));
        //Set BridgeIp!
        strcpy(bridgeIP, MDNS.IP(i).toString().c_str());
        
        Serial.print(":");
        Serial.print(MDNS.port(i));
        Serial.println(")");
        Serial.print("New bridge IP String: ");
        Serial.println(bridgeIP);
        validBridgeIP = true;
      }
    }
  }
}

/*
 * Checks Bridge connection and Api-Key
 */
void checkConnectionAndApiKey(){
  HTTPClient http;
  
  char * testPath = (char *) malloc(sizeof(char)*500);
  strcpy(testPath, totalApiKeyPath);
  strcat(testPath, "groups/");
  //int retVal = http.begin(testPath); //totalApiKeyPath
  int retVal = http.begin(testPath);
  if(retVal){
    int succ = http.GET();
    if(succ > 0){
      //Serial.println(http.getString());
      
      // If Error -> string has [ ] at beginning/end -> must be removed for parsing!!! Aren't there if theres no error.
      char * requestResult = (char *) malloc(sizeof(char) * 5000);
      char * editedResult = (char *) malloc(sizeof(char) * 5000);
      strcpy(requestResult, http.getString().c_str());
      
      c_remove_squarebrackets(requestResult, editedResult);
      Serial.print("edited json: ");
      Serial.println(editedResult);
      JSONVar result = JSON.parse(editedResult);
      
      
      if(result.hasOwnProperty("error")){
        if(strcmp(result["error"]["description"], "unauthorized user")){
          Serial.println("API-Key invalid. Unauthorized user");
          apiKeyValid = false;
        } else {
          Serial.println("Test!!");
          Serial.println(result["error"]["description"]);
        }
      } else {
        Serial.println("Json doesnt have property Error");
        Serial.println("Successfully checked connection");
        //Serial.println(result["error"]["description"]);
        validBridgeIP = true;
        apiKeyValid = true;
      }
    } else {
      Serial.println("HTTP-Call failed. Hostname might be incorrect.");
      validBridgeIP = false;
    }
  }else{ // is never reached....
    Serial.println("IP/Hostname are not reachable");
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
 * Get Device Status (ON/OFF)
 * For groupName
 */
int getGroupStatus(char * groupName){
  int id = getGroupID(groupName);
  char sID[5];

  /*
   * Convert groupName and PATH to one Path.
   */
  sprintf(sID, "%d", id);
  char * name = (char *) malloc(sizeof(char)*100);
  strcpy(name, totalApiKeyPath);
  strcat(name, "groups/");
  JSONVar jsonRes;// = (JSONVar * ) malloc(sizeof(JSONVar));

  /*
   * Get Request for resulting Path
   */
  getRequest(strcat(name, sID), jsonRes);
  
  Serial.print("Group Status: ");
  Serial.println(jsonRes["action"]["on"]);
  Serial.println(JSON.stringify(jsonRes["action"]["on"]).c_str());

  /*
   * Convert Result to 0/1
   */
  if(!strcmp(JSON.stringify(jsonRes["action"]["on"]).c_str(), String("true").c_str())){
    Serial.println("Returning from getGroupStatus true");
    return 1;
  } else {
    Serial.println("Returning from getGroupStatus false");
    return 0;
  }
}
/*
 * path: Get Request URL
 * inVar: Pointer to JSONVar, where the result shall be stored.
 */
int getRequest(const char * path, JSONVar& inVar, bool https){
  /*
   * HTTP Get Request
   */
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(https){
    http.begin(path, root_ca);
  } else {
    http.begin(path);
  }
  http.GET();
  String payload = http.getString();
  Serial.println(payload);
  /*
   * JSON Parsing
   */
  inVar = JSON.parse(payload);
  if(JSON.typeof(inVar) == "undefined") {
    Serial.println("Parsing input failed getRequest!");
    Serial.println(payload);
    return 1;
  }
  http.end();
  return 0;
}

int postRequest(char * path, char * body, JSONVar& jsonVar){
  HTTPClient http;
  http.begin(path);
  http.POST(body);
  String payload = http.getString();
  Serial.println(payload);
  char * result = (char *) malloc(sizeof(char)*300);
  strcpy(result, payload.c_str()); // copy to remove error [
  char * cleanedResult = (char *) malloc(sizeof(char) *300);
  c_remove_squarebrackets(result, cleanedResult);
  Serial.println(cleanedResult);
  
  jsonVar = JSON.parse(cleanedResult);
  http.end();
  if(JSON.typeof(jsonVar) == "undefined") {
    Serial.println("Post Parsing input failed!");
    return 1;
  }
  free(cleanedResult);
  free(result);
  return 0;
}

int putRequest(char * path, char * body, JSONVar& jsonVar){
  HTTPClient http;
  http.begin(path);
  http.PUT(body);
  Serial.println(body);
  String payload = http.getString();
  Serial.println(payload);
  jsonVar = JSON.parse(payload);

  if(JSON.typeof(jsonVar) == "undefined") {
    Serial.println("Put Request Parsing input failed!");
    return 1; // Try again?
  }
  http.end();
  return 0;
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

void updateLightSchedule(char * scheduleName, bool state, char * timeIn, char * groupName){
  Serial.println("update get schedule ID for");
  Serial.println(scheduleName);
  int schedID = getScheduleID(scheduleName);
  Serial.println("Got schedule ID");
  char * path = (char *) malloc(sizeof(char) * 100);
  strcpy(path, totalApiKeyPath); 
  strcat(path, "schedules/");
  JSONVar resJson;
  char * cState = (char *) malloc(sizeof(char)*6);
  if(state){
    strcpy(cState, "true");
  } else {
    strcpy(cState, "false");
  }

  if(schedID){
    int groupId = getGroupID(groupName);
    Serial.println("Update Schedule");
    // Update Schedule  
    char * message = (char *) malloc(sizeof(char) * 200);
    snprintf(message, sizeof(char)*200, "{\"localtime\": \"%s\", \"command\": { \"address\": \"/api/%s/groups/%d/action\", \"body\": { \"on\": %s, \"bri\": 254}, \"method\": \"PUT\"}}", timeIn, apiKey, groupId, cState);
    strcat(path, String(schedID).c_str());
    Serial.println(message);
    while(putRequest(path, message, resJson)){ // -> Try again if failed
      delay(1000);
    }
    Serial.println("Updated Schedule");
    free(message);
  }else{
    // Create Schedule
    Serial.println("Create Schedule");
    char * message = (char *) malloc(sizeof(char) * 300);
    int groupID = getGroupID(groupName);
    Serial.println("Got Group ID");
    
    if(groupID){
      snprintf(message, sizeof(char)*299, "{\"name\": \"%s\", \"description\": \"My wake up alarm\", \"command\": { \"address\": \"/api/%s/groups/%d/action\", \"method\": \"PUT\", \"body\": { \"on\": %s, \"bri\": 254 } }, \"localtime\": \"%s\"}", scheduleName, apiKey, groupID, cState, timeIn);
      Serial.println("Try postRequest");
      Serial.print("Request: ");
      Serial.println(message);
      Serial.print("Path: ");
      Serial.println(path);
      while(postRequest(path, message, resJson)){
        delay(1000);
      }
    }else{
      Serial.println("Error, group not found");
    }
    Serial.println("Created Schedule");
    free(message);
    free(cState);
  }
  free(path);
}

/*
 * Similar to getGroupID, gets the ScheduleID for a given scheduleName
 * so that this ID can then be used, to modify, delete, replace.... the Schedule.
 */
int getScheduleID(char * scheduleName){
  HTTPClient http;
  char * path = (char * ) malloc(sizeof(char)*100);
  strcpy(path, totalApiKeyPath); //apiKeyPath
  strcat(path, "schedules/");
  http.begin(path);
  http.GET();
  String payload = http.getString();  
  Serial.println(payload);
  http.end();

  JSONVar myObject = JSON.parse(payload);
  int index = 1;
  while(index < MAX_DEVICE_GROUPS){
    //Serial.print(index);
    //Serial.println(myObject[String(index)]);
    //if(myObject[String(index)]){ // Geht wahrscheinlich nicht.
    if(strcmp(JSON.stringify(myObject[String(index)]).c_str(), "null")){  
      Serial.println(myObject[String(index)]);
      Serial.println(myObject[String(index)]["name"]);
      if(!strcmp(myObject[String(index)]["name"], scheduleName)){
        Serial.print("Korrekte Schedule ID ist: ");
        Serial.println(index, DEC);
        break;
      }
    }
    index++;
  }
  free(path);
  if(index == 256)
    return 0;
  return index;
}

int getGroupID(char * groupName){
  /*
   * Get complete group topography via HTTP GET
   */
  HTTPClient http;
  char * groupPath = (char*) malloc(sizeof(char)*100);
  strcpy(groupPath, totalApiKeyPath);
  strcat(groupPath, "groups/");
  Serial.print("getGroupID, path: ");
  Serial.println(groupPath);
  http.begin(groupPath); 
  http.GET();
  String payload = http.getString();
  http.end();
  Serial.print("Payload: ");
  Serial.println(payload);

  /*
   * Convert to JSON
   */
  JSONVar myObject = JSON.parse(payload);
  if(JSON.typeof(myObject) == "undefined") {
    Serial.println("Parsing input failed, getGroupID!");
    return 0;
  }
  Serial.print("JSON Object = ");
  Serial.println(myObject);
  int index = 1;
  Serial.println(myObject.keys());
  
  /*
   * Iterate over all Device groups until correct one is found
   */
  while(index < MAX_DEVICE_GROUPS){
    Serial.print(index);
    Serial.println(myObject[String(index)]);
    if(!strcmp(myObject[String(index)]["name"],groupName)){
      Serial.print("Korrekte ID ist: ");
      Serial.println(index, DEC);
      break; 
    }
    index++;
  }

  /*
   * If no device is found
   */
  if(index == MAX_DEVICE_GROUPS){
    Serial.println("Keine Gruppe mit dem Namen gefunden!"); 
  }else{
    Serial.print("Korrekte ID ist: ");
    Serial.println(index, DEC);
    
  }
  free(groupPath);
  return index;
}

void deleteRequest(char * scheduleName){
  int schedID = getScheduleID(scheduleName);
  if(schedID == 0){
    Serial.println("ScheduleID doesn't exist!");
  }else{
    HTTPClient http;
    char * deletePath = (char *) malloc(sizeof(char) * 150);
    strcpy(deletePath, totalApiKeyPath);
    strcat(deletePath, "schedules/");
    strcat(deletePath, String(schedID).c_str());
    http.begin(deletePath);
    http.sendRequest("DELETE");
    Serial.print("Delete result: ");
    Serial.println(http.getString());
    http.end();
    free(deletePath);
  }
}

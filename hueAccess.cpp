#include "hueAccess.hpp"

bool validBridgeIP = false;
bool apiKeyValid = false;

char * totalApiKeyPath;
char * apiKey;
char * bridgeIP;

char * controlledGroupName;  // -> Group to control

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

void troubleshootConnection() {
  if(!validBridgeIP){
      Serial.println("Bridge not connected, try new connection");
      getIpViaMDNS(); // Connect to Bridge (-> update IP adress, if changed)

      // again check the connection, IP/hostname should now be correct, check for API Key
      checkConnectionAndApiKey();
      if(!validBridgeIP){
        Serial.println("Bridge still not connected! There might be an error in your configuration.");
        display2->showNumberHexEx(err_bridgeIP, 0, false, 4, 0);
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
            display2->showNumberHexEx(err_apiKey, 0, false, 4, 0);
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
        display2->showNumberHexEx(err_apiKey, 0, false, 4, 0);
        Serial.println("API-Key still invalid, will try to get new on");
        getAndSaveNewAPIKey(10);
      }
      checkConnectionAndApiKey();
      if(!apiKeyValid){
        Serial.println("API-Key still invalid, personal inspection needed");
        display2->showNumberHexEx(err_apiKey, 0, false, 4, 0);
        errorState = err_apiKey;
      } else {
        generateTotalApiKeyPath();
        saveConfigFile();
      }
    }
  }
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

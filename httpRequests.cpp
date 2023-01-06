#include "httpRequests.hpp"

void deleteRequest(char * scheduleName){
  int schedID = getScheduleID(scheduleName);
  Serial.println("Trying to delete Schedule");
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
    //Serial.print("Delete result: ");
    //Serial.println(http.getString());
    http.end();
    free(deletePath);
  }
}

int putRequest(char * path, char * body, JSONVar& jsonVar){
  HTTPClient http;
  http.begin(path);
  http.PUT(body);
  
  String payload = http.getString();
  
  jsonVar = JSON.parse(payload);

  if(JSON.typeof(jsonVar) == "undefined") {
    Serial.println("Put Request Parsing input failed!");
    Serial.println(body);
    Serial.println(payload);
    return 1; // Try again?
  }
  http.end();
  return 0;
}

int postRequest(char * path, char * body, JSONVar& jsonVar){
  HTTPClient http;
  http.begin(path);
  http.POST(body);
  String payload = http.getString();
  //Serial.println(payload);
  char * result = (char *) malloc(sizeof(char)*300);
  strcpy(result, payload.c_str()); // copy to remove error [
  char * cleanedResult = (char *) malloc(sizeof(char) *300);
  c_remove_squarebrackets(result, cleanedResult);
  Serial.println(cleanedResult);
  
  jsonVar = JSON.parse(cleanedResult);
  http.end();
  if(JSON.typeof(jsonVar) == "undefined") {
    Serial.println("Post Parsing input failed!");
    Serial.println(payload);
    return 1;
  }
  free(cleanedResult);
  free(result);
  return 0;
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
  //Serial.println(payload);
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
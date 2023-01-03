#ifndef hueAccess
#define hueAccess

#include <stdbool.h>
#include "httpRequests.hpp"
#include <Arduino_JSON.h>
#include "cjson/cJSON.h"
#include "Arduino.h"
#include <ESPmDNS.h>
#include "espWEB.h"
#include "espWebErrorCodes.h"

extern int errorState;

#define MAX_DEVICE_GROUPS 256

extern char * totalApiKeyPath;
extern char * apiKey;
extern char * bridgeIP;

extern bool validBridgeIP;
extern bool apiKeyValid;
extern char * controlledGroupName;  // -> Group to control

/*
 * get internal ID of group
 * @param groupName name of the group (i.e. Roomname... single Lamps aren't supported)
 * @result ID of group
 */
int getGroupID(char * groupName);


/*
 * get status of the group
 * @param groupName name of the group (i.e. Roomname... single Lamps aren't supported)
 * @result 1 = on, 0 = off
 */
int getGroupStatus(char * groupName);

/*
 * get the schedule ID for further use (e.g. URL generation)
 * @param scheduleName name of the searched schedule
 */
int getScheduleID(char * scheduleName);

/*
 * method for updating/creating a schedule
 * @param scheduleName
 * @param state turn on or off (true/false)
 * @param timeIn specifically formatted time string, at which the schedule shall be executed
 * @param groupName name of the group which shall be affected
 */
void updateLightSchedule(char * scheduleName, bool state, char * timeIn, char * groupName);

/*
 * connect to the Bridge, i.e. find the IP-Adress
 */
void connectToBridge();

/*
 * check Connectionparameters (IP and APIKey)
 */
void checkConnection();

/*
 * Checks Bridge connection and Api-Key
 */
void checkConnectionAndApiKey();

/*
 * configure the APIKey
 */
void configureAPIKey(int timeout);

void getIpViaMDNS();

/*
 * Configure new API Key
 * timeout: how often shall getting a new Key be tried
 * will try to get a key all 15 seconds, as long as timeout isnt reached
 */
void getAndSaveNewAPIKey(int timeout);


void troubleshootConnection();

void c_remove_squarebrackets(char * input, char * output);

void generateTotalApiKeyPath();
#endif

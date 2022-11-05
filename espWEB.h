#ifndef espWEB
#define espWEB

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
 *  make a postRequest to the URL dictated by path
 *  @param path URL, mostly API Calls
 *  @param body JSON body, e.g. for configuring routines/schedules
 *  @return returns JSONVar for accessing the returned JSON
 */ 
int postRequest(char * path, char * body, JSONVar& jsonVar);

/*
 * make a getRequest
 * @param path URL
 * @param inVar inVar for storing result
 * @param https, whether call shall be executed by https, needs valid root_ca
 * 
 */
int getRequest(const char * path, JSONVar& inVar, bool https = false);

/*
 * register a button interrupt
 * @param pin the pin (GPIO) number
 * @param event the event which the interrupt handler will listen for FALLING, RISING.... 
 * @param targetFunction ISR function to be executed
 * @param modus ... the GPIO mode usually INPUT for a button
 */
int registerButton(uint8_t pin, int event, void (*targetFunction)(void), int modus);
/*
 * switch the configured Lightgroup
 */ 
int switchLights();

/*
 * get the schedule ID for further use (e.g. URL generation)
 * @param scheduleName name of the searched schedule
 */
int getScheduleID(char * scheduleName);

/*
 * make a put request, e.g. for creating schedules
 * @param path the URL
 * @param body the JSON body
 * @param jsonVar variable for returns
 */
int putRequest(char * path, char * body, JSONVar& jsonVar);

/*
 * method for updating/creating a schedule
 * @param scheduleName
 * @param state turn on or off (true/false)
 * @param timeIn specifically formatted time string, at which the schedule shall be executed
 * @param groupName name of the group which shall be affected
 */
void updateLightSchedule(char * scheduleName, bool state, char * timeIn, char * groupName);

/*
 * method for deleting an existing schedule
 * @param scheduleName name of the schedule to be deleted
 */
void deleteRequest(char * scheduleName);

/*
 * Helper Function for modulo operation (positive AND negative!!)
 */
int mod(int a, int b);

/*
 * initialize a rotaryEncoder with two pins
 * @param pinA, pinB Numbers for the two pins
 * @param Function pointer to ISRs
 * @param event for Interrupt 
 */ 
void initRotaryEncoder(int pinA, int pinB, void (*targetFunctionA)(void), void (*targetFunctionB)(void), int eventA, int eventB);

/*
 * Store a JSON Variable to SPIFFS
 * @param to bestored json
 * @param path where it should be stored
 */ 
bool jsonVarToSPIFFS(JSONVar& json, char * storePath);

/*
 * connect to the Bridge, i.e. find the IP-Adress
 */
void connectToBridge();

/*
 * check Connectionparameters (IP and APIKey)
 */
void checkConnection();

/*
 * configure the APIKey
 */
void configureAPIKey(int timeout);

/*
 * check Reset values, and in case, start the configuration Panel
 */
void checkForReset(WiFiManager& manager);

/*
 * set Time values
 */
void configureTime();

/*
 * Register I/O Stuff (Buttons) 
 */
void registerInputs();

void updateAlarmDisplay();

void updateRemoteAlarm();

//String serverName = "http://philipshuebridge/api/8LZjhVl65zPrJiqEPTWkmRaggDv1YzJQBxi1GhcZ/groups/";

//irgendein ROOT_CA
/*const char * root_ca = \ 
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n" \
"-----END CERTIFICATE-----\n"; */

// Philips HUE Root_ca
const char * root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIICOTCCAd+gAwIBAgIHF4j//m8+sDAKBggqhkjOPQQDAjA+MQswCQYDVQQGEwJO\n" \
"TDEUMBIGA1UECgwLUGhpbGlwcyBIdWUxGTAXBgNVBAMMEDAwMTc4OGZmZmU2ZjNl\n" \
"YjAwIhgPMjAxNzAxMDEwMDAwMDBaGA8yMDM4MDEwMTAwMDAwMFowPjELMAkGA1UE\n" \
"BhMCTkwxFDASBgNVBAoMC1BoaWxpcHMgSHVlMRkwFwYDVQQDDBAwMDE3ODhmZmZl\n" \
"NmYzZWIwMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEQkN8PN9NrJIS7ReGkzxL\n" \
"RmbQdwL0Wi8gPWCqcALrJ7rccvKDV3GKLIS/snotbX+zUc/Aj+iE6iVPoal2bxFa\n" \
"JKOBwzCBwDAMBgNVHRMBAf8EAjAAMB0GA1UdDgQWBBTAFfbXZ66ecVwGxdDbnUDX\n" \
"ypvwKDBsBgNVHSMEZTBjgBTAFfbXZ66ecVwGxdDbnUDXypvwKKFCpEAwPjELMAkG\n" \
"A1UEBhMCTkwxFDASBgNVBAoMC1BoaWxpcHMgSHVlMRkwFwYDVQQDDBAwMDE3ODhm\n" \
"ZmZlNmYzZWIwggcXiP/+bz6wMA4GA1UdDwEB/wQEAwIFoDATBgNVHSUEDDAKBggr\n" \
"BgEFBQcDATAKBggqhkjOPQQDAgNIADBFAiEA1vxgWLiN1SFk3UPDgSBVrITqnkDp\n" \
"tJb1vDSB6IXTkY8CICyGg3H5H3D+Fq3I5Bt0/LGsx8IRNUpiSF4f0u9PmYkM\n" \
"-----END CERTIFICATE-----";



#endif

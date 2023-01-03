#ifndef httpRequests
#define httpRequests

#include <Arduino_JSON.h>
#include "cjson/cJSON.h"
#include "Arduino.h"
#include <HTTPClient.h>
#include <HTTP_Method.h>
#include "hueAccess.hpp"


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
 * make a put request, e.g. for creating schedules
 * @param path the URL
 * @param body the JSON body
 * @param jsonVar variable for returns
 */
int putRequest(char * path, char * body, JSONVar& jsonVar);

/*
 * method for deleting an existing schedule
 * @param scheduleName name of the schedule to be deleted
 */
void deleteRequest(char * scheduleName);


static const char * root_ca = \
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

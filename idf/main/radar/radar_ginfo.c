#include "radar_ginfo.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static char response[8192]; static size_t used;
static esp_err_t data(esp_http_client_event_t *e) { if(e->event_id==HTTP_EVENT_ON_DATA && used+e->data_len<sizeof(response)){memcpy(response+used,e->data,e->data_len);used+=e->data_len;} return ESP_OK; }
static bool request(const char *url, const char *post) { used=0; esp_http_client_config_t c={.url=url,.event_handler=data,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=8000}; esp_http_client_handle_t h=esp_http_client_init(&c); if(post){esp_http_client_set_method(h,HTTP_METHOD_POST);esp_http_client_set_header(h,"Content-Type","application/json");esp_http_client_set_post_field(h,post,strlen(post));} esp_err_t r=esp_http_client_perform(h); esp_http_client_cleanup(h); response[used]=0; return r==ESP_OK; }
static void get(cJSON *r,const char*n,char*d,size_t z){cJSON*x=cJSON_GetObjectItemCaseSensitive(r,n);snprintf(d,z,"%s",cJSON_IsString(x)?x->valuestring:"");}
bool radar_ginfo_lookup(const char *hex, radar_ginfo_t *o){memset(o,0,sizeof(*o));char body[80],url[96];snprintf(body,sizeof(body),"{\"ICAO24BitHex\":\"%s\",\"IncludeDeregistered\":false}",hex);if(!request("https://ginfoapi.caa.co.uk/api/aircraft/search",body))return false;cJSON*r=cJSON_Parse(response);cJSON*x=r?cJSON_GetArrayItem(r,0):NULL;cJSON*id=x?cJSON_GetObjectItem(x,"AircraftID"):NULL;if(!cJSON_IsNumber(id)){cJSON_Delete(r);return false;}snprintf(url,sizeof(url),"https://ginfoapi.caa.co.uk/api/aircraft/details/%d",id->valueint);cJSON_Delete(r);if(!request(url,NULL))return false;r=cJSON_Parse(response);if(!r)return false;cJSON*a=cJSON_GetObjectItem(r,"AircraftDetails"),*owners=cJSON_GetObjectItem(r,"RegisteredAircraftOwners"),*op=cJSON_GetObjectItem(r,"AircraftOperatedByAocHolder");get(a,"Type",o->type,sizeof(o->type));cJSON*xyear=cJSON_GetObjectItem(a,"YearBuild");if(cJSON_IsNumber(xyear))snprintf(o->year,sizeof(o->year),"%d",xyear->valueint);get(cJSON_GetArrayItem(owners,0),"RegisteredOwner",o->owner,sizeof(o->owner));get(op,"OperatorName",o->operator_name,sizeof(o->operator_name));cJSON*rd=cJSON_GetObjectItem(r,"RegistrationDetails");get(rd,"Mark",o->registration,sizeof(o->registration));cJSON_Delete(r);return true;}
static radar_ginfo_t pending; static volatile bool ready, busy; static char requested_hex[7];
static void worker(void *arg){char hex[7];snprintf(hex,sizeof(hex),"%s",(const char *)arg);radar_ginfo_lookup(hex,&pending);ready=true;busy=false;vTaskDelete(NULL);}
bool radar_ginfo_start(const char *hex){if(busy||!hex||strlen(hex)!=6)return false;ready=false;busy=true;snprintf(requested_hex,sizeof(requested_hex),"%s",hex);return xTaskCreate(worker,"ginfo",12288,requested_hex,5,NULL)==pdPASS;}
bool radar_ginfo_take(radar_ginfo_t *out){if(!ready)return false;ready=false;*out=pending;return true;}

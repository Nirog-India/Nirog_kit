#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>


#include "cJSON.h"
#include "esp_event_loop.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_http_client.h"

#include "booth_server.h"
#include "temperature.h"
#include "heartrate.h"
#include "resprate.h"
#include "battery_monitor.h"
#include "led_notification.h"

#include "lwip/api.h"
#include "websocket_server.h"
#include "wifimanager.h"

static const char *TAG = "Server";


#define TAKE_OXY_READING "OXYMETER"
#define TAKE_TEMP_READING "THERMOMETER"
#define CONNECT_WIFI "WIFI"
#define DETAILS "DETAILS"
#define DISCONNECT_MSG_CODE "DISCONNECT"

 enum LED led1[3] = {RED1_LED,BLUE1_LED,GREEN1_LED};
enum LED led2[3] = {RED2_LED,BLUE2_LED,GREEN2_LED};

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}



 int ubidots_post(char* post_data){
    int status_code = 0; 
    esp_http_client_config_t client_config = {
        .url = "http://things.ubidots.com/api/v1.6/devices/esp?token=BBFF-qfdAOBNngUyWFyYXAnWK3yvyD6oO4i",
        .event_handler = _http_event_handler,
        .port = 80
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    return status_code;
}

static QueueHandle_t client_queue;
const static int client_queue_size = 10;


float temp_handler() {
    blink_led_start(LED_GRP1,true,true,false);
    float temp= get_temperature();
    blink_led_stop();
    ESP_LOGI(TAG,"Temperature is : %f",temp);
    return temp;
}

// char post_data[50];

oxy_reading oxy_handler() {
    
    blink_led_start(LED_GRP1,false,true,true);
    oxy_reading oxymeter_reading = get_oxy_result();
    blink_led_stop();
    return oxymeter_reading;
}


// serves any clients
static void http_serve(struct netconn *conn) {
  struct netbuf* inbuf;
  static uint8_t *buf;
  static char out_buf[256];
  static uint16_t buflen;
  static err_t err;

  char post_data[128];
  float temp_reading = 0;
  oxy_reading oxy_result;

  netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
  ESP_LOGI(TAG,"reading from client...");
  char name[32];
  char phno[18];
  while(1){
    err = netconn_recv(conn, &inbuf);
    printf(".\n");
    if(err == ERR_OK) {
      netbuf_data(inbuf, (void**)&buf, &buflen);
      printf("\nread : %s\n",buf);
      cJSON *client_msg_root = cJSON_Parse((char*)buf);
      if (client_msg_root == NULL) {
        ESP_LOGI(TAG,"JSON root is null");
      }else{
        const cJSON *event_code = cJSON_GetObjectItemCaseSensitive(client_msg_root, "CODE");
        const cJSON *message = cJSON_GetObjectItemCaseSensitive(client_msg_root, "MESSAGE");
        printf("\n message code %s\n",event_code->valuestring);
        printf("\n Data %s\n ", message-> valuestring);
        if(strcmp(event_code->valuestring,DETAILS) == 0){
          cJSON *details_root = cJSON_Parse((char*)message->valuestring);
          cJSON *details_name = cJSON_GetObjectItemCaseSensitive(details_root, "NAME");
          cJSON *details_no = cJSON_GetObjectItemCaseSensitive(details_root, "PHONE");
          sprintf(name,"%s",details_name->valuestring);
          sprintf(phno,"%s",details_no->valuestring);

        }
        if(strcmp(event_code->valuestring,TAKE_OXY_READING) == 0){
          oxy_result = oxy_handler();
          char oxymeter_response[128]; 
          sprintf(oxymeter_response,"{\"CODE\": %s,\"MESSAGE\" : {\"HR\" : %3.1f , \"SPO\" : %3.1f}}",TAKE_OXY_READING,oxy_result.finalheartRate,oxy_result.oxygenLevel);

          printf("\nHeart rate is %3.1f Spo2 is %3.1f",oxy_result.finalheartRate,oxy_result.oxygenLevel);
          netconn_write(conn, oxymeter_response,strlen(oxymeter_response),NETCONN_COPY);
          led_success_notification();
        }
        if(strcmp(event_code->valuestring,TAKE_TEMP_READING) == 0){
          char temp_response[256];
          temp_reading = temp_handler();
          sprintf(temp_response,"{\"CODE\": %s,\"MESSAGE\" : %f }",TAKE_TEMP_READING,temp_reading);
          netconn_write(conn, temp_response,strlen(temp_response),NETCONN_COPY);        
          led_success_notification();
        }
        if(strcmp(event_code->valuestring,CONNECT_WIFI) == 0){
            // start_battery_check();
          char wifi_response[256];
          printf("\nsocket data : %s", message->valuestring);
          blink_led_start(LED_GRP1,true,false,true);
          bool connection_result = websocket_connect_wifi_handler(message->valuestring);
          blink_led_stop();
          if(connection_result){
              sprintf(wifi_response,"{\"CODE\": %s,\"MESSAGE\" : \"Wifi Connection successfull\" }",CONNECT_WIFI);
              light_led(BLUE1_LED);
          }else{
            sprintf(wifi_response,"{\"CODE\": %s,\"MESSAGE\" : \"Wifi Connection unsuccessfull\" }",CONNECT_WIFI);
            led_clear_all();
            led_multi_notification(RED1_LED,GREEN1_LED);
          }
          netconn_write(conn, wifi_response,strlen(wifi_response),NETCONN_COPY);        
        }
        if(strcmp(event_code->valuestring,DISCONNECT_MSG_CODE) == 0){
          ESP_LOGI(TAG,"Reading completed . Pushing to Server .");
          
          sprintf(post_data,"temperature=%4.2f;heartrate=%3.1f;oxygen_level=%3.1f",temp_reading,oxy_result.finalheartRate,oxy_result.oxygenLevel); 
          int push_status = ubidots_post(post_data);
          char disconnect_response[512];
          char report[256];
        sprintf(report,"{\"NAME\":\"%s\",\"PHONE\":%s,\"HR\":%3.1f,\"SPO\":%3.1f,\"TEMPERATURE\":%f,\"STATUS\":%d}",name,phno,oxy_result.finalheartRate,oxy_result.oxygenLevel,temp_reading,push_status);
          sprintf(disconnect_response,"{\"CODE\":%s,\"MESSAGE\":%s}",DISCONNECT_MSG_CODE,report);
          ESP_LOGI(TAG,"%s",report);
          ESP_LOGI(TAG,"%s", disconnect_response);
          netconn_write(conn, disconnect_response,strlen(disconnect_response),NETCONN_COPY);
          break;
          }
      }
    }
  }
}


static void server_task(void* pvParameters) {
  const static char* TAG = "server_task";
  struct netconn *conn, *newconn;
  static err_t err;
  client_queue = xQueueCreate(client_queue_size,sizeof(struct netconn*));

  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn,NULL,80);
  netconn_listen(conn);
  ESP_LOGI(TAG,"server listening");
  do {
    err = netconn_accept(conn, &newconn);
    ESP_LOGI(TAG,"new client");
    if(err == ERR_OK) {
      xQueueSendToBack(client_queue,&newconn,portMAX_DELAY);
      //http_serve(newconn);
    }
  } while(err == ERR_OK);
  netconn_close(conn);
  netconn_delete(conn);
  ESP_LOGE(TAG,"task ending, rebooting board");
  esp_restart();
}

static void server_handle_task(void* pvParameters) {
  const static char* TAG = "server_handle_task";
  struct netconn* conn;
  ESP_LOGI(TAG,"task starting");
  for(;;) {
    xQueueReceive(client_queue,&conn,portMAX_DELAY);
    printf("Added in queue\n");
    if(!conn) continue;
    http_serve(conn);
  }
  vTaskDelete(NULL);
}

static void count_task(void* pvParameters) {
  const static char* TAG = "count_task";
  char out[20];
  int len;
  int clients;
  const static char* word = "%i";
  uint8_t n = 0;
  const int DELAY = 1000 / portTICK_PERIOD_MS; // 1 second

  ESP_LOGI(TAG,"starting task");
  for(;;) {
    len = sprintf(out,word,n);
    clients = ws_server_send_text_all(out,len);
    if(clients > 0) {
      //ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
    }
    n++;
    vTaskDelay(DELAY);
  }
}

bool start_webserver(void) {
    
    ESP_LOGI(TAG, "Starting Wifi API websocket server");

    ws_server_start();
    xTaskCreate(&server_task,"server_task",3000,NULL,9,NULL);
    xTaskCreate(&server_handle_task,"server_handle_task",4000,NULL,6,NULL);
    xTaskCreate(&count_task,"count_task",6000,NULL,2,NULL);
    return true;
}
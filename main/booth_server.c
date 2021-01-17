#include <math.h>
#include <stdio.h>
#include <stdbool.h>
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
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_heap_trace.h"

#include "booth_server.h"
#include "temperature.h"
#include "heartrate.h"
#include "resprate.h"
#include "battery_monitor.h"
#include "led_notification.h"

#include "lwip/api.h"
#include "websocket_server.h"
#include "wifimanager.h"
#include "battery_monitor.h"

static const char *TAG = "Server";

uint8_t *recv_buf;
const int recv_msg_len = 256;
bool conn_alive;
bool finger_not_placed = true;

#define END_OF_MESSAGE "_EOM_"
#define TAKE_OXY_READING "OXYMETER"
#define TAKE_TEMP_READING "THERMOMETER"
#define CONNECT_WIFI "WIFI"
#define DETAILS "DETAILS"
#define DISCONNECT_MSG_CODE "DISCONNECT"
#define PING_MSG_CODE "PING"
#define ERROR_ACK_MSG_CODE "ERROR"

#define NUM_RECORDS 500
static heap_trace_record_t trace_record[NUM_RECORDS];

bool is_pinging = false;
enum LED led1[3] = {RED1_LED,BLUE1_LED,GREEN1_LED};
enum LED led2[3] = {RED2_LED,BLUE2_LED,GREEN2_LED};

enum READING_CODE{
  IDLE,
  READ_OXYGEN_HEARTRATE,
  READ_TEMPERATURE,
  DISCONNECT
}CODE;

char name[32];
char phno[18];

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
    int push_counter= 0;
    bool push_incomplete = true;
    int push_retry_counter = 0;
    while(push_incomplete){
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
                    push_incomplete = false;
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %d %s",err ,esp_err_to_name(err));
            if(push_retry_counter++ > 5) push_incomplete = false;
            
        }
    }
    esp_http_client_cleanup(client);
    return status_code;
}

static QueueHandle_t client_queue;
static QueueHandle_t message_queue;
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

int64_t msg_recv_timestamp = 0;

static void ping(struct netconn *conn) {
    struct netbuf* inbuf;
    static uint8_t *buf;
    static char out_buf[256];
    static uint16_t buflen;
    static err_t err = -1;
    char ping_msg[64];
    int con_code;
    int64_t curr_time = 0;           //Reset value after n days to prevent overflow 
    int64_t prev_time = 0;
    char ping_msg_code[20];
    bool isDisconnect = false; 
    
    con_code = esp_timer_get_time();
    msg_recv_timestamp = con_code;    
    sprintf(ping_msg_code,"%d",con_code);
    sprintf(ping_msg,"{\"CODE\" : \"%s\", \"MESSAGE\" : %s}",PING_MSG_CODE,ping_msg_code);
    message_queue = xQueueCreate(client_queue_size,recv_msg_len);    
    
    netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second      
    CODE = IDLE;      
    while(!isDisconnect){
        // if(err == ERR_OK) netbuf_alloc(inbuf,256);

        err = netconn_recv(conn, &inbuf);
        // Precess the received message if message received, otherwise print error code
        if(err == ERR_OK){ 
          // Transfer received data into recv_buf                          
          netbuf_data(inbuf, (void**)&recv_buf, &buflen);          
          netbuf_free(inbuf); 
          netbuf_alloc(inbuf,256);         
          ESP_LOGI(TAG,"\n Recv : %s",recv_buf);
          // printf("\n REcv len : %d",strlen((char*)recv_buf));  
                    
          int recv_msg_size= strlen((char*)recv_buf);               
          int recv_len = 0;
          // Separating from garbage                    
          while(recv_buf[recv_len++] != '\n'){
            if(recv_len >= recv_msg_size)break;
          }          
          char recv_msg_trimmed[recv_len+1];                    
          int cntr = 0;
          recv_len -= 5;
          // Separate individual messages
          for(int i=0 ; i <= recv_len; i++){
            recv_msg_trimmed[cntr++] = recv_buf[i];
            if(recv_buf[i+1] == '_' && recv_buf[i+2] == 'E' && recv_buf[i+3] == 'O' && recv_buf[i+4] == 'M' && recv_buf[i+5] == '_'){
              // End string and move i to start of next message
              recv_msg_trimmed[cntr] = '\0';
              i = i+5;
              cntr = 0;
              ESP_LOGI(TAG,"\n trim : %s", recv_msg_trimmed);
              
              // Process individual message
              cJSON *client_msg_root = cJSON_Parse(recv_msg_trimmed);
              
              if (client_msg_root == NULL) {
                // JSON parse unsuccessful
                ESP_LOGI(TAG,"JSON root is null");
                char recv_error_response[64];
                sprintf(recv_error_response,"{\"CODE\": %s,\"MESSAGE\" : \"ERR_ACK\"}",ERROR_ACK_MSG_CODE);
                xQueueSendToBack(message_queue,&recv_error_response,portMAX_DELAY);

              }else{
                const cJSON *event_code = cJSON_GetObjectItemCaseSensitive(client_msg_root, "CODE");
                const cJSON *message = cJSON_GetObjectItemCaseSensitive(client_msg_root, "MESSAGE");
                // printf("\nReacv : %s, \n",event_code->valuestring);
                if(strcmp(event_code->valuestring,DETAILS) == 0){
                  cJSON *details_root = cJSON_Parse((char*)message->valuestring);
                  cJSON *details_name = cJSON_GetObjectItemCaseSensitive(details_root, "NAME");
                  cJSON *details_no = cJSON_GetObjectItemCaseSensitive(details_root, "PHONE");
                  
                  sprintf(name,"%s",details_name->valuestring);
                  sprintf(phno,"%s",details_no->valuestring);
                  ESP_LOGI(TAG,"Details : name %s ph no : %s",name,phno);

                  cJSON_Delete(details_root);

                }
                if(strcmp(event_code->valuestring,TAKE_OXY_READING) == 0){
                  CODE = READ_OXYGEN_HEARTRATE;
                }
                if(strcmp(event_code->valuestring,TAKE_TEMP_READING) == 0){
                  CODE = READ_TEMPERATURE;
                }
                if(strcmp(event_code->valuestring,CONNECT_WIFI) == 0){
                  char wifi_response[256];
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
                  // netconn_write(conn, wifi_response,strlen(wifi_response),NETCONN_COPY);
                  xQueueSendToBack(message_queue,&wifi_response,portMAX_DELAY); 
                }
                if(strcmp(event_code->valuestring,DISCONNECT_MSG_CODE) == 0){
                  CODE = DISCONNECT;
                  }
                if(strcmp(event_code->valuestring,PING_MSG_CODE) == 0){
                  ESP_LOGI(TAG,"\n Ping received");
                  
                  msg_recv_timestamp = esp_timer_get_time();
                  // if(strcmp(message->valuestring,ping_msg_code) == 0){
                  //     printf("\n string matched");
                  //     msg_recv_timestamp = curr_static QueueHandle_t client_queue;time;
                  // }
                }
              }
              cJSON_Delete(client_msg_root);
            }                    
          }
          /////////////////////////////////////////////////////////////          
        }
        else{
          ESP_LOGE(TAG,"NETCONN RECV ERROR CODE : %d",err);
        }
        // netbuf_free(inbuf);
        if(CODE == READ_OXYGEN_HEARTRATE && finger_not_placed){
            char oxymeter_warning[128];
            ESP_LOGI(TAG,"Finger not placed!!!");
            sprintf(oxymeter_warning,"{\"CODE\": %s,\"MESSAGE\" : \"ERR_OXY_FINGER_NOT_PLACED\" }",ERROR_ACK_MSG_CODE);
            xQueueSendToBack(message_queue,&oxymeter_warning,portMAX_DELAY);
        }
        while(uxQueueMessagesWaiting(message_queue) > 0){
          char msg_buf[recv_msg_len];
          xQueueReceive(message_queue,&msg_buf,portMAX_DELAY);
          strcat(msg_buf,END_OF_MESSAGE);
          // printf("\n netconn write message : %s",msg_buf);
          netconn_write(conn, msg_buf,strlen(msg_buf),NETCONN_COPY);
          if(!conn_alive) isDisconnect = true;
          vTaskDelay(1000/portTICK_PERIOD_MS);
        }        
        curr_time = esp_timer_get_time();
        if((curr_time - prev_time)/5000000.0f>1)
        {
          ESP_LOGI(TAG,"\n time bc : %f",(curr_time - prev_time)/1000000.0f);
          prev_time = curr_time;
          xQueueSendToBack(message_queue,&ping_msg,portMAX_DELAY);          
        }
        if((curr_time - msg_recv_timestamp)/30000000.0f > 1){
          isDisconnect = true;
          conn_alive = false;
        }
        vTaskDelay(100/portTICK_PERIOD_MS);        
  }
   vQueueDelete(message_queue);
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
  ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
  netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
  ESP_LOGI(TAG,"reading from client..."); 
  while(conn_alive){
    // err = netconn_recv(conn, &inbuf);
    
    //printf("Battery Level: %d\n",raw_bat_reading);
    if(raw_bat_reading <2000)
    {
      ESP_LOGI(TAG,"Low battery");
    }

    if(CODE == READ_OXYGEN_HEARTRATE){
        oxy_result = oxy_handler();
        char oxymeter_response[256]; 
        sprintf(oxymeter_response,"{\"CODE\": %s,\"MESSAGE\" : {\"HR\" : %3.1f , \"SPO\" : %3.1f}}",TAKE_OXY_READING,oxy_result.finalheartRate,oxy_result.oxygenLevel);

        ESP_LOGI(TAG,"\nHeart rate is %3.1f Spo2 is %3.1f",oxy_result.finalheartRate,oxy_result.oxygenLevel);
        // netconn_write(conn, oxymeter_response,strlen(oxymeter_response),NETCONN_COPY);
        xQueueSendToBack(message_queue,&oxymeter_response,portMAX_DELAY);

        led_success_notification();
        CODE = IDLE;
    }
    if(CODE == READ_TEMPERATURE){
        char temp_response[256];
        temp_reading = temp_handler();
        sprintf(temp_response,"{\"CODE\": %s,\"MESSAGE\" : %f }",TAKE_TEMP_READING,temp_reading);
        // netconn_write(conn, temp_response,strlen(temp_response),NETCONN_COPY); 
        xQueueSendToBack(message_queue,&temp_response,portMAX_DELAY);
        led_success_notification();
        CODE = IDLE;
    }    
    if(CODE == DISCONNECT){
        ESP_LOGI(TAG,"Reading completed . Pushing to Server .");            
        sprintf(post_data,"temperature=%4.2f;heartrate=%3.1f;oxygen_level=%3.1f",temp_reading,oxy_result.finalheartRate,oxy_result.oxygenLevel); 
        int push_status = ubidots_post(post_data);
        // int push_status = 0;
        char disconnect_response[512];
        char report[256];
        sprintf(report,"{\"NAME\":\"%s\",\"PHONE\":\"%s\",\"HR\":%3.1f,\"SPO\":%3.1f,\"TEMPERATURE\":%f,\"STATUS\":%d}",name,phno,oxy_result.finalheartRate,oxy_result.oxygenLevel,temp_reading,push_status);
        sprintf(disconnect_response,"{\"CODE\":%s,\"MESSAGE\":%s}",DISCONNECT_MSG_CODE,report);
        ESP_LOGI(TAG,"%s",report);
        ESP_LOGI(TAG,"%s", disconnect_response);
        xQueueSendToBack(message_queue,&disconnect_response,portMAX_DELAY);                  
        conn_alive = false;
        break;
    }
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
  ESP_LOGI(TAG,"Session break. Mem %d",xPortGetFreeHeapSize());
      ESP_ERROR_CHECK( heap_trace_stop() );
    heap_trace_dump();
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
    if(conn_alive){
      vTaskDelay(100/portTICK_PERIOD_MS);
      continue;
    }
    err = netconn_accept(conn, &newconn);
    ESP_LOGI(TAG,"new client");
    conn_alive = true;
    if(err == ERR_OK) {
      xQueueSendToBack(client_queue,&newconn,portMAX_DELAY);
      //http_serve(newconn);
    }
  } while(err == ERR_OK);
  netconn_close(conn);
  netconn_delete(conn);
  ESP_LOGE(TAG,"task ending, rebooting board");
  // esp_restart();
}

static void connection_status_task(void* pvParameters) {
  const static char* TAG = "connection_status_task";
  struct netconn* conn;
  ESP_LOGI(TAG,"Ping starting");
  for(;;) {
    xQueuePeek(client_queue,&conn,portMAX_DELAY);
    is_pinging = true;
    ESP_LOGI(TAG,"Ping Started");
    if(!conn) continue;
    ping(conn);
  }
  vTaskDelete(NULL);
}


static void server_handle_task(void* pvParameters) {
  const static char* TAG = "server_handle_task";
  struct netconn* conn;
  ESP_LOGI(TAG,"task starting");
  for(;;) {
    if(!is_pinging) {
      vTaskDelay(100/portTICK_PERIOD_MS);
      continue;
      }else{
        is_pinging = false;
      }
      
    xQueueReceive(client_queue,&conn,portMAX_DELAY);
    ESP_LOGI(TAG,"Added in queue\n");
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

    ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

    ws_server_start();
    xTaskCreate(&server_task,"server_task",3000,NULL,9,NULL);
    xTaskCreate(&server_handle_task,"server_handle_task",6000,NULL,6,NULL);
    xTaskCreate(&count_task,"count_task",6000,NULL,2,NULL);
    xTaskCreate(&connection_status_task,"connection_task",3000,NULL,9,NULL);
    return true;
}
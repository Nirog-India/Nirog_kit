#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <sys/types.h>


#include <stdio.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "lwip/api.h"
#include "websocket_server.h"
#include "socket.h"


static const char *TAG = "WEBSOCKET";


static QueueHandle_t client_queue;
const static int client_queue_size = 10;

// handles websocket events
void websocket_callback(uint8_t num,WEBSOCKET_TYPE_t type,char* msg,uint64_t len) {
  const static char* TAG = "websocket_callback";
  int value;

  switch(type) {
    case WEBSOCKET_CONNECT:
      ESP_LOGI(TAG,"client %i connected!",num);
      break;
    case WEBSOCKET_DISCONNECT_EXTERNAL:
      ESP_LOGI(TAG,"client %i sent a disconnect message",num);
      // led_duty(0);
      break;
    case WEBSOCKET_DISCONNECT_INTERNAL:
      ESP_LOGI(TAG,"client %i was disconnected",num);
      break;
    case WEBSOCKET_DISCONNECT_ERROR:
      ESP_LOGI(TAG,"client %i was disconnected due to an error",num);
      // led_duty(0);
      break;
    case WEBSOCKET_TEXT:
      if(len) { // if the message length was greater than zero
        switch(msg[0]) {
          case 'L':
            if(sscanf(msg,"L%i",&value)) {
              ESP_LOGI(TAG,"LED value: %i",value);
              // led_duty(value);
              ws_server_send_text_all_from_callback(msg,len); // broadcast it!
            }
            break;
          case 'M':
            ESP_LOGI(TAG, "got message length %i: %s", (int)len-1, &(msg[1]));
            break;
          default:
	          ESP_LOGI(TAG, "got an unknown message with length %i", (int)len);
	          break;
        }
      }
      break;
    case WEBSOCKET_BIN:
      ESP_LOGI(TAG,"client %i sent binary message of size %i:\n%s",num,(uint32_t)len,msg);
      break;
    case WEBSOCKET_PING:
      ESP_LOGI(TAG,"client %i pinged us with message of size %i:\n%s",num,(uint32_t)len,msg);
      break;
    case WEBSOCKET_PONG:
      ESP_LOGI(TAG,"client %i responded to the ping",num);
      break;
  }
}


#define TEST1_MSG_CODE 1
#define TEST2_MSG_CODE 2
#define TEST3_MSG_CODE 0
#define DISCONNECT_MSG_CODE 127
// serves any clients
static void http_serve(struct netconn *conn) {
  const static char* TAG = "http_server";
  struct netbuf* inbuf;
  static uint8_t *buf;
  static char out_buf[256];
  static uint16_t buflen;
  static err_t err;

  netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
  ESP_LOGI(TAG,"reading from client...");
  int write_ret = 0;
  while(1){
    err = netconn_recv(conn, &inbuf);
    // ESP_LOGI(TAG,"read from client . ret : %d",err);
    if(err == ERR_OK) {
      netbuf_data(inbuf, (void**)&buf, &buflen);
      printf("\nread : %x\n",*buf);

      if(*buf == TEST1_MSG_CODE){
        printf("\n Test 1 called");
        *out_buf = 23;
      }
      if(*buf == TEST2_MSG_CODE){
        printf("\n Test 2 called");
        *out_buf = "69.69";
      }
      if(*buf == TEST3_MSG_CODE){
        printf("\n Test 3 called");
        sprintf(out_buf,"hello from the other side");
      }
      // if(*buf == DISCONNECT_MSG_CODE){
      //   isConnected = false;
      // }
      netconn_write(conn, out_buf,strlen(out_buf),NETCONN_COPY);
      
      
      
      printf("\n");
      if(*buf == DISCONNECT_MSG_CODE){
        printf("disconnected \n");
        printf("\n");
      
        break;
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


void websocket_app_start(void)
{
    ws_server_start();
    xTaskCreate(&server_task,"server_task",3000,NULL,9,NULL);
    xTaskCreate(&server_handle_task,"server_handle_task",4000,NULL,6,NULL);
    xTaskCreate(&count_task,"count_task",6000,NULL,2,NULL);
}

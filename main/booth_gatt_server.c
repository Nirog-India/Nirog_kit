#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"


#include "sdkconfig.h"
#include "heartrate.h"
#include "reading.h"
#include "system_routine.h"


#define GATTS_TAG "GATT_SERVER"


static void gatts_reading_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);


#define GATTS_SERVICE_UUID_SENSOR   0x00AA
#define GATTS_CHAR_UUID_READING         0xAA01
#define GATTS_CHAR_UUID_STATUS_FINGER      0xAA02
#define GATTS_CHAR_UUID_STATUS_BATTERY      0xAA03
#define GATTS_DESCR_UUID_READING     0x0666
#define GATTS_NUM_HANDLE_READING     10

#define MAX_CHAR_NUM 3

uint16_t GATTS_CHAR_UUID_ARR[MAX_CHAR_NUM] = {GATTS_CHAR_UUID_READING,GATTS_CHAR_UUID_STATUS_FINGER,GATTS_CHAR_UUID_STATUS_BATTERY};



#define DEVICE_NAME            "BOOTH_GATT_SERVER"

#define GATT_CHAR_VAL_LEN_MAX 0x40

#define PREPARE_BUF_MAX_SIZE 1024

#define MAX_STATUS_INTERVAL 1000000.0f

#define MAX_BATTERY_CAP 180    //mV (batt range)

reading sensor_readings;
bool reading_complete = true;
typedef struct client_cred_tag{
    esp_gatt_if_t gatts_if;
    esp_ble_gatts_cb_param_t *param;
}client_cred;

client_cred curr_client;

static uint8_t char_read_str[] = {0x72,0x65,0x61,0x64};
static uint8_t char_finger_status_str[] = {0x46,0x49,0x4e,0x47,0x45,0x52};
static uint8_t char_battery_status_str[] = {0x46,0x49,0x4e,0x47,0x45,0x52};
static esp_gatt_char_prop_t booth_property = 0;

static esp_attr_value_t gatt_char_arr[MAX_CHAR_NUM] = {
    {
    .attr_max_len = GATT_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char_read_str),
    .attr_value   = char_read_str,
    },
    {
    .attr_max_len = GATT_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char_finger_status_str),
    .attr_value   = char_finger_status_str,
    },
    {
    .attr_max_len = GATT_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char_battery_status_str),
    .attr_value   = char_battery_status_str,
    }

};

int char_counter = 0;

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

static uint8_t adv_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
    //second uuid, 32bit, [12], [13], [14], [15] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

//adv data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

//scan response data

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define PROFILE_NUM 1
#define PROFILE_READING_ID 0




struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle[MAX_CHAR_NUM];
    esp_bt_uuid_t char_uuid[MAX_CHAR_NUM];
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle[MAX_CHAR_NUM];
    esp_bt_uuid_t descr_uuid[MAX_CHAR_NUM];
};


static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_READING_ID] = {
        .gatts_cb = gatts_reading_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t reading_prepare_write_env;

void write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void status_updater();

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed\n");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising stop failed\n");
        } else {
            ESP_LOGI(GATTS_TAG, "Stop adv successfully\n");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTS_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

void write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){    //RRemove after testing
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.need_rsp){
        if (param->write.is_prep){
            if (prepare_write_env->prepare_buf == NULL) {
                prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE*sizeof(uint8_t));
                prepare_write_env->prepare_len = 0;
                if (prepare_write_env->prepare_buf == NULL) {
                    ESP_LOGE(GATTS_TAG, "Gatt_server prep no mem\n");
                    status = ESP_GATT_NO_RESOURCES;
                }
            } else {
                if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_OFFSET;
                } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_ATTR_LEN;
                }
            }

            esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK){
               ESP_LOGE(GATTS_TAG, "Send response error\n");
            }
            free(gatt_rsp);
            if (status != ESP_GATT_OK){
                return;
            }
            memcpy(prepare_write_env->prepare_buf + param->write.offset,
                   param->write.value,
                   param->write.len);
            prepare_write_env->prepare_len += param->write.len;

        }else{
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
        }
    }
}

void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
        esp_log_buffer_hex(GATTS_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

void send_result(){
    uint8_t rsp[36];
    char* scan_result_data = (char*)malloc(36);
    sprintf(scan_result_data,"%d,%3.2f,%3.2f,%3.2f,%3.2f",sensor_readings.heartrate,sensor_readings.heart_precision,sensor_readings.oxygenLevel,sensor_readings.oxy_precision,sensor_readings.temperature);
    for(int i = 0; i<strlen(scan_result_data);i++){
        rsp[i] = scan_result_data[i];
    }
    ESP_LOGI("GATT_SERVER","Sent to Tab %s ",scan_result_data);
    esp_ble_gatts_send_indicate(curr_client.gatts_if, curr_client.param->write.conn_id, gl_profile_tab[PROFILE_READING_ID].char_handle[0],strlen(scan_result_data),&rsp, false);
}

void send_result_cb(void (*scan_result)()){
    (*scan_result)();
}


static void reading_task(void* pvParameters){

    void (*result)() = &send_result;
    sensor_readings = take_reading();
    reading_complete = true;
    send_result_cb(result);

    vTaskDelete(NULL);
} 

void send_battery_status(){
    uint8_t battery_status_value=0;
    battery_status_value = (raw_bat_reading-460)*100/MAX_BATTERY_CAP;
    // printf("raw : %d, bat per : %d\n",(int)raw_bat_reading,(int)battery_status_value);
    esp_ble_gatts_send_indicate(curr_client.gatts_if, curr_client.param->write.conn_id, gl_profile_tab[PROFILE_READING_ID].char_handle[2],sizeof(battery_status_value),&battery_status_value, false);
}

void send_battery_status_cb(void (*battery_status)()){
    (*battery_status)();
}

void send_finger_status(){
    uint8_t finger_status_value;
    if(finger_not_placed) finger_status_value = 0x00;
    else finger_status_value = 0x01;
    esp_ble_gatts_send_indicate(curr_client.gatts_if, curr_client.param->write.conn_id, gl_profile_tab[PROFILE_READING_ID].char_handle[1],sizeof(finger_status_value),&finger_status_value, false);
}

void send_finger_status_cb(void (*battery_status)()){
    (*battery_status)();
}

static void status_update_task(void* pvParameters){
    int64_t curr_time = 0;
    int64_t prev_time = 0;
    void (*battery)() = &send_battery_status; 
    void (*finger)() = &send_finger_status;
    bool prev_finger_state = finger_not_placed;
    while(1){
        curr_time = esp_timer_get_time();
        if(curr_time - prev_time > MAX_STATUS_INTERVAL){
            send_battery_status_cb(battery);
            prev_time = curr_time;
        }
        if(!reading_complete) {
            if(prev_finger_state != finger_not_placed){
                ESP_LOGI("FInger","Blimey %s %s",prev_finger_state?"true":"false",finger_not_placed?"true":"false");
                send_finger_status_cb(finger);
                prev_finger_state = finger_not_placed;
            }
        }
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}
static void gatts_reading_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_profile_tab[PROFILE_READING_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_READING_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_READING_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_READING_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_SENSOR;

        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(DEVICE_NAME);
        if (set_dev_name_ret){
            ESP_LOGE(GATTS_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }

        //config adv data
        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret){
            ESP_LOGE(GATTS_TAG, "config adv data failed, error code = %x", ret);
        }
        adv_config_done |= adv_config_flag;
        //config scan response data
        ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (ret){
            ESP_LOGE(GATTS_TAG, "config scan response data failed, error code = %x", ret);
        }
        adv_config_done |= scan_rsp_config_flag;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_READING_ID].service_id, GATTS_NUM_HANDLE_READING);
        break;
    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        esp_gatt_status_t status = ESP_GATT_OK;
        esp_gatt_rsp_t response;
        memset(&response, 0, sizeof(esp_gatt_rsp_t));
        response.attr_value.handle = param->write.handle;
        response.attr_value.len = 1;
        if (!param->write.is_prep){
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
            esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
            if(param->read.handle == gl_profile_tab[PROFILE_READING_ID].char_handle[0] && reading_complete)
            {
                reading_complete = false;
                xTaskCreate(&reading_task,"Reading_task",3000,NULL,8,NULL);
                curr_client.gatts_if = gatts_if;
                curr_client.param = param;
                response.attr_value.value[0] = 0;
            }else response.attr_value.value[0] = 1;
            if(param->write.handle == gl_profile_tab[PROFILE_READING_ID].char_handle[2])
            {
                curr_client.gatts_if = gatts_if;
                curr_client.param = param;
                xTaskCreate(&status_update_task,"status_update_task",3000,NULL,9,NULL);
            }
        }
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, &response);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(GATTS_TAG,"ESP_GATTS_EXEC_WRITE_EVT");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        exec_write_event_env(&reading_prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_profile_tab[PROFILE_READING_ID].service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_READING_ID].service_handle);
        gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter].len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter].uuid.uuid16 = GATTS_CHAR_UUID_ARR[char_counter];


        booth_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_READING_ID].service_handle, &gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter],
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            booth_property,
                                                            &gatt_char_arr[char_counter], NULL);
        if (add_char_ret){
            ESP_LOGE(GATTS_TAG, "add char failed, error code =%x",add_char_ret);
        }
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t length = 0;
        const uint8_t *prf_char;
        
        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        gl_profile_tab[PROFILE_READING_ID].char_handle[char_counter] = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_READING_ID].descr_uuid[char_counter].len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_READING_ID].descr_uuid[char_counter].uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,  &length, &prf_char);
        if (get_attr_ret == ESP_FAIL){
            ESP_LOGE(GATTS_TAG, "ILLEGAL HANDLE");
        }

        ESP_LOGI(GATTS_TAG, "the gatts demo char length = %x\n", length);
        for(int i = 0; i < length; i++){
            ESP_LOGI(GATTS_TAG, "prf_char[%x] =%x\n",i,prf_char[i]);
        }
        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_READING_ID].service_handle, &gl_profile_tab[PROFILE_READING_ID].descr_uuid[char_counter],
                                                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        if (add_descr_ret){
            ESP_LOGE(GATTS_TAG, "add char descr failed, error code =%x", add_descr_ret);
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_READING_ID].descr_handle[0] = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        char_counter++;
        if(char_counter < MAX_CHAR_NUM ){
            ESP_LOGI(GATTS_TAG,"Adding next character");
            gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter].len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter].uuid.uuid16 = GATTS_CHAR_UUID_ARR[char_counter];
            add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_READING_ID].service_handle, &gl_profile_tab[PROFILE_READING_ID].char_uuid[char_counter],
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            booth_property,
                                                            &gatt_char_arr[char_counter], NULL);


            if (add_char_ret){
            ESP_LOGE(GATTS_TAG, "add char failed, error code =%x",add_char_ret);
        }
        }
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT: {
        BLE_DISCONNECTED = false;
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_READING_ID].conn_id = param->connect.conn_id;
        //start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        BLE_DISCONNECTED = true;

        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_CONF_EVT:
        // ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d\n",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void start_gatt_server(void)
{
    esp_err_t ret;

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gap register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_READING_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    return;
}



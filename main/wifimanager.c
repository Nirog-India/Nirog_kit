#include <esp_http_server.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>


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

#include "wifimanager.h"


/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* GPIO PIN to indicate WIFI status */
#define WIFISTATUS_GPIO 02

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE (200 * 1024)  // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE 8192

#define ESP_WIFI_SSID "Covid"
#define ESP_WIFI_PASS "password"
#define MAX_STA_CONN 3

#define MAX_ROUTES 40
typedef struct tagWIFI_CONFIG {
    uint8_t ssid[32];
    uint8_t password[64];
} WIFI_CONFIG;

#define STORAGE_NAMESPACE "storage"

WIFI_CONFIG *wifiConfig = NULL;
WIFI_CONFIG *backupWifiConfig = NULL;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;
bool connecting = false;

static const char *TAG = "wifi station";

static int s_retry_num = 0;
static int ap_retry_num = 0;

#define MAX_RETRY 10

httpd_handle_t handle;

int apMode = 0;
int wifiConnected = 0;

void toggle_wifi_status_led() {
    gpio_pad_select_gpio(WIFISTATUS_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(WIFISTATUS_GPIO, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "Toggling WIfi status LED.");
    gpio_set_level(WIFISTATUS_GPIO, 1);
    if (wifiConnected == 1) {
        ESP_LOGD(TAG, "Turning on the LED");
        gpio_set_level(WIFISTATUS_GPIO, 1);
    } else {
        ESP_LOGD(TAG, "Turning off the LED");
        gpio_set_level(WIFISTATUS_GPIO, 0);
    }
}

int save_wifi_settings() {
    esp_err_t err;
    // Open
    ESP_LOGI(TAG, "Opening NVS handle...to save wifi settings ");
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(my_handle, "wifiConfig", wifiConfig, sizeof(WIFI_CONFIG));
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    err = nvs_commit(my_handle);
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
    // Close
    nvs_close(my_handle);
    return 0;
}

esp_err_t connect_wifi_to_station(char *ssid, char *password) {
    wifi_config_t wifi_config = {
        .sta = {.ssid = "", .password = ""},
    };
    if (wifiConfig == NULL) wifiConfig = (WIFI_CONFIG *)malloc(sizeof(WIFI_CONFIG));
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifiConfig->ssid, ssid, sizeof(wifiConfig->ssid));
    memcpy(wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    memcpy(wifiConfig->password, password, sizeof(wifiConfig->password));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "Attempting to connect to wifi %s. Pass: %s", wifi_config.sta.ssid,
             wifi_config.sta.password);
    s_retry_num = 0;
    ap_retry_num = 0;
    wifiConnected = 0;
    esp_wifi_connect();
    toggle_wifi_status_led();
    /* Send a simple response */
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                          void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (apMode == 0) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (apMode) {
            if (s_retry_num < MAX_RETRY) {
                s_retry_num++;
                esp_wifi_connect();
            } else {
                esp_wifi_disconnect();
                wifiConnected = -1;
            }
        } else {
            if (ap_retry_num < MAX_RETRY) {
                ap_retry_num++;
                esp_wifi_connect();
            } else {
                wifiConnected = -1;
                esp_wifi_disconnect();
                ESP_LOGI(TAG,
                         "Unable to connect to Given access point.try again");
                connecting = false;

            }
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "connect to the AP fail %d", event->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        connecting = false;
        wifiConnected = 1;
        toggle_wifi_status_led();
        save_wifi_settings();
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

int wifimanager_connected() { return wifiConnected; }

void wifi_onConnection() {
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, 0, 1, portMAX_DELAY);
}

void wifi_init_sta() {
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {.ssid = "", .password = ""},
    };
    memcpy(wifi_config.sta.ssid, wifiConfig->ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, wifiConfig->password, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", wifiConfig->ssid, wifiConfig->password);
}

void wifi_init_softap(void) {
    /*
    INitializes Access Point with given configuration.
    */

    s_wifi_event_group = xEventGroupCreate();
    tcpip_adapter_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    uint8_t chipid[6];
    esp_efuse_mac_get_default(chipid);
    wifi_config_t wifi_config = {
        .ap = {.password = ESP_WIFI_PASS,
               .max_connection = MAX_STA_CONN,
               .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    // sprintf((char *)wifi_config.ap.ssid, "%s-%02x%02x%02x%02x", ESP_WIFI_SSID, chipid[2], chipid[3],
    //         chipid[4], chipid[5]);
    sprintf((char *)wifi_config.ap.ssid, "%s", ESP_WIFI_SSID);
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);

    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", wifi_config.ap.ssid,
             wifi_config.ap.password);
}

bool websocket_connect_wifi_handler(char* wifi_cred) {


    // int ret = httpd_req_recv(req, content, recv_size)
    ESP_LOGI(TAG,"Connect wifi called");
    cJSON *root = cJSON_Parse(wifi_cred);
    if (root == NULL) {
        ESP_LOGI(TAG,"JSON root is null");
        return false;
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "pass");
    if ((ssid == NULL) || !cJSON_IsString(ssid) || (password == NULL) ||
        !cJSON_IsString(password) || (strlen(ssid->valuestring) > 32) ||
        (strlen(password->valuestring) > 64)) {
        cJSON_Delete(root);
        ESP_LOGI(TAG,"error parsing request, invalid data");
        return false;
    }
    esp_err_t error = connect_wifi_to_station(ssid->valuestring, password->valuestring);
    if (error != ESP_OK) {
        ESP_LOGI(TAG, "Failed to connect to a wifi network");
        return false;
    }
    // const char resp[] = "{\"success\": true}";

    cJSON_Delete(root);
    return true;
}

esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            ESP_LOGI(TAG, "%.*s", evt->data_len, (char *)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

int wifiHaveConfig = 0;

int wifimanager() {
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
        ESP_LOGI(TAG, "erasing nvs flash");
    }
    // Open
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t my_handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_blob(my_handle, "wifiConfig", NULL, &required_size);
    if (required_size > 0) {
        // we have config
        wifiConfig = (WIFI_CONFIG *)malloc(required_size);
        backupWifiConfig = (WIFI_CONFIG *)malloc(required_size);
        err = nvs_get_blob(my_handle, "wifiConfig", wifiConfig, &required_size);
        // err = nvs_get_blob(my_handle, "wifiConfig", backupWifiConfig, &required_size); //backup
        // wifi feature
        wifiHaveConfig = 1;
    } else {
        wifiHaveConfig = 0;
    }
    // Close
    nvs_close(my_handle);
    return 0;
}

int wifimanager_start() {
    wifimanager();
    if (wifiHaveConfig == 1) {
        // TODO  - Better handle wifi siwtching and station mode starts
        wifi_init_softap();
        wifi_init_sta();
    } else {
        apMode = 1;
        wifi_init_softap();
    }
    return 0;
}

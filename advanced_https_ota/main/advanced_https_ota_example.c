#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "lwip/dns.h"
#include "esp_netif.h"
#include "cJSON.h"

// Configuración de la red WiFi
const char* ssid = "SBC";
const char* password = "SBCwifi$";

// Configuración de ThingsBoard
const char* token = "7hShB0A3icIlzPFxROsH";  // Token de autenticación de tu dispositivo en ThingsBoard
const char* thingsboard_host = "https://demo.thingsboard.io/api/v1/";
const char* attributes_endpoint = "/attributes";  // Endpoint para obtener atributos del dispositivo

static const char *TAG = "HTTPS_OTA";

// Maneja eventos de WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Intentando reconectar al WiFi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado a WiFi. Dirección IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Inicializa el WiFi
void wifi_init() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "SBC",
            .password = "SBCwifi$"
        }
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// Función para realizar la actualización OTA
void perform_ota_update(const char* firmware_url) {
    esp_http_client_config_t ota_config = {
        .url = firmware_url,
    };

    ESP_LOGI(TAG, "Iniciando actualización OTA desde %s", firmware_url);
    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Actualización OTA exitosa, reiniciando...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Error en actualización OTA: %s", esp_err_to_name(ret));
    }
}

// Función para obtener la URL de OTA desde ThingsBoard
void get_ota_url_from_thingsboard() {
    char url[256];
    snprintf(url, sizeof(url), "%s%s%s", thingsboard_host, token, attributes_endpoint);

    esp_http_client_config_t config = {
        .url = url,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    char auth_header[100];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int len = esp_http_client_get_content_length(client);
        char *buffer = malloc(len + 1);
        esp_http_client_read(client, buffer, len);
        buffer[len] = 0;

        cJSON *json = cJSON_Parse(buffer);
        if (json) {
            cJSON *firmware_url = cJSON_GetObjectItem(json, "firmware_url");
            if (cJSON_IsString(firmware_url) && (firmware_url->valuestring != NULL)) {
                ESP_LOGI(TAG, "URL de firmware obtenida: %s", firmware_url->valuestring);
                perform_ota_update(firmware_url->valuestring);
            }
            cJSON_Delete(json);
        }
        free(buffer);
    } else {
        ESP_LOGE(TAG, "Error en la solicitud HTTP: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();       // Inicializa la conexión WiFi
    vTaskDelay(pdMS_TO_TICKS(5000));
    get_ota_url_from_thingsboard();  // Obtiene la URL de firmware y aplica la actualización OTA
}

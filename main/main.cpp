#include <iostream>
#include <string>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"


#include "driver/gpio.h"
#include "esp_timer.h"   
extern "C" {
    void app_main(void);
}

// ==================== НАСТРОЙКИ - ИЗМЕНИ ЭТО! ====================
// WiFi настройки
const char* WIFI_SSID = "RT52";        // <-- ИЗМЕНИ
const char* WIFI_PASS = "11194000";      // <-- ИЗМЕНИ

// MQTT настройки
const char* MQTT_BROKER_URI = "mqtt://broker.emqx.io";  // <-- ИЗМЕНИ если нужно
const char* MQTT_CMD_TOPIC = "/esp32_1/command";        // <-- ИЗМЕНИ
const char* MQTT_STATUS_TOPIC = "/esp32_1/status";      // <-- ИЗМЕНИ

// Пин светодиода
const gpio_num_t LED_PIN = GPIO_NUM_18;      // <-- ИЗМЕНИ если нужно
// ================================================================

// ==================== WiFi класс ====================
class WiFiManager {
private:
    static const char* TAG;
    const char* ssid;
    const char* password;
    
    static void event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
        WiFiManager* self = static_cast<WiFiManager*>(arg);
        
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(self->TAG, "Подключение к WiFi...");
            esp_wifi_connect();
        } 
        else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(self->TAG, "❌ WiFi отключен, переподключаюсь...");
            esp_wifi_connect();
        } 
        else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(self->TAG, "✅ WiFi подключен. IP: " IPSTR, 
                     IP2STR(&event->ip_info.ip));
        }
    }
    
public:
    WiFiManager(const char* wifi_ssid, const char* wifi_pass) 
        : ssid(wifi_ssid), password(wifi_pass) {
        TAG = "WiFi";
    }
    
    bool init() {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, MQTT_EVENT_ANY, &event_handler, this, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, NULL));
        
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, ssid);
        strcpy((char*)wifi_config.sta.password, password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(TAG, "WiFi инициализация завершена");
        return true;
    }
    
    int get_rssi() {
        wifi_ap_record_t info;
        if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
            return info.rssi;
        }
        return 0;
    }
};

const char* WiFiManager::TAG = nullptr;

// ==================== MQTT класс ====================
class MQTTManager {
private:
    static const char* TAG;
    esp_mqtt_client_handle_t client;
    std::string broker_uri;
    std::string command_topic;
    std::string status_topic;
    bool led_state;
    WiFiManager* wifi;  // для получения RSSI
    
    static void mqtt_event_handler(void* arg, esp_event_base_t base,
                                   int32_t event_id, void* event_data) {
        MQTTManager* self = static_cast<MQTTManager*>(arg);
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        
        switch (event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(self->TAG, "✅ MQTT подключен к %s", self->broker_uri.c_str());
                esp_mqtt_client_subscribe(self->client, 
                                          self->command_topic.c_str(), 1);
                ESP_LOGI(self->TAG, "📡 Подписались на %s", 
                         self->command_topic.c_str());
                self->publish("ESP32 готов к работе");
                self->send_status();
                break;
                
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(self->TAG, "❌ MQTT отключен");
                break;
                
            case MQTT_EVENT_DATA: {
                std::string message(event->data, event->data_len);
                ESP_LOGI(self->TAG, "📩 Получено: %s", message.c_str());
                self->process_command(message);
                break;
            }
                
            default:
                break;
        }
    }
    
    void process_command(const std::string& message) {
        cJSON* json = cJSON_Parse(message.c_str());
        
        if (json == nullptr) {
            // Простая текстовая команда
            if (message == "ON") {
                led_state = true;
                gpio_set_level(LED_PIN, 1);
                ESP_LOGI(TAG, "💡 LED включен");
                send_status();
            }
            else if (message == "OFF") {
                led_state = false;
                gpio_set_level(LED_PIN, 0);
                ESP_LOGI(TAG, "💡 LED выключен");
                send_status();
            }
            else if (message == "STATUS") {
                send_status();
            }
            return;
        }
        
        // JSON команда
        cJSON* cmd = cJSON_GetObjectItem(json, "command");
        if (cmd && cmd->valuestring) {
            if (strcmp(cmd->valuestring, "LED") == 0) {
                cJSON* state = cJSON_GetObjectItem(json, "value");
                if (state && state->valuestring) {
                    if (strcmp(state->valuestring, "ON") == 0) {
                        led_state = true;
                        gpio_set_level(LED_PIN, 1);
                    } else {
                        led_state = false;
                        gpio_set_level(LED_PIN, 0);
                    }
                    ESP_LOGI(TAG, "💡 LED: %s", state->valuestring);
                }
            }
            else if (strcmp(cmd->valuestring, "STATUS") == 0) {
                send_status();
            }
        }
        
        cJSON_Delete(json);
        send_status();
    }
    
    void send_status() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "led", led_state ? "ON" : "OFF");
        cJSON_AddNumberToObject(root, "rssi", wifi->get_rssi());
        cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
        
        char* json_str = cJSON_Print(root);
        publish(json_str);
        
        free(json_str);
        cJSON_Delete(root);
        ESP_LOGI(TAG, "📊 Статус отправлен");
    }
    
    void publish(const char* message) {
        if (client) {
            esp_mqtt_client_publish(client, status_topic.c_str(), 
                                    message, 0, 1, 0);
        }
    }
    
public:
    MQTTManager(const char* uri, const char* cmd_topic, 
                const char* stat_topic, WiFiManager* wifi_mgr) 
        : broker_uri(uri), command_topic(cmd_topic), 
          status_topic(stat_topic), led_state(false), wifi(wifi_mgr) {
        TAG = "MQTT";
    }
    
    void start() {
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = broker_uri.c_str();
        
        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, 
                                        &mqtt_event_handler, this);
        esp_mqtt_client_start(client);
    }
};

const char* MQTTManager::TAG = nullptr;

// ==================== LED класс ====================
class LED {
private:
    gpio_num_t pin;
    bool state;
    static const char* TAG;
    
public:
    LED(gpio_num_t gpio_pin) : pin(gpio_pin), state(false) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
        ESP_LOGI(TAG, "LED инициализирован на GPIO %d", pin);
    }
    
    void on() {
        state = true;
        gpio_set_level(pin, 1);
        ESP_LOGI(TAG, "LED ON");
    }
    
    void off() {
        state = false;
        gpio_set_level(pin, 0);
        ESP_LOGI(TAG, "LED OFF");
    }
    
    void toggle() {
        state = !state;
        gpio_set_level(pin, state ? 1 : 0);
        ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
    }
    
    bool getState() const { return state; }
};

const char* LED::TAG = "LED";

// ==================== Главный класс ====================
class ESP32App {
private:
    WiFiManager wifi;
    MQTTManager mqtt;
    LED led;
    static const char* TAG;
    
public:
    ESP32App() : 
        wifi(WIFI_SSID, WIFI_PASS),                    // ← используем глобальные переменные
        mqtt(MQTT_BROKER_URI, MQTT_CMD_TOPIC, 
             MQTT_STATUS_TOPIC, &wifi),                // ← используем глобальные переменные
        led(LED_PIN) {                                  // ← используем глобальную переменную
        TAG = "MAIN";
    }
    
    void init() {
        ESP_LOGI(TAG, "=");
        ESP_LOGI(TAG, "Запуск ESP32 MQTT приложения");
        ESP_LOGI(TAG, "WiFi SSID: %s", WIFI_SSID);
        ESP_LOGI(TAG, "MQTT Broker: %s", MQTT_BROKER_URI);
        ESP_LOGI(TAG, "Команды топик: %s", MQTT_CMD_TOPIC);
        ESP_LOGI(TAG, "Статус топик: %s", MQTT_STATUS_TOPIC);
        ESP_LOGI(TAG, "LED пин: %d", LED_PIN);
        ESP_LOGI(TAG, "=");
        
        // Инициализация NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        
        // Подключаем WiFi
        wifi.init();
        
        // Ждем WiFi
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        
        // Запускаем MQTT
        mqtt.start();
        
        ESP_LOGI(TAG, "✅ Приложение запущено");
    }
    
    void run() {
        while (1) {
            vTaskDelay(10000 / portTICK_PERIOD_MS); // ничего не делаем, всё в событиях
        }
    }
};

const char* ESP32App::TAG = nullptr;

// ==================== Точка входа ====================
void app_main(void) {
    static ESP32App app;
    app.init();
    app.run();
}
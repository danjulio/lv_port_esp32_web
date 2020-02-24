/* SPI Master example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lvgl/lvgl.h"
#include "lv_examples/lv_apps/demo/demo.h"
#include "esp_freertos_hooks.h"

#include "websocket_driver.h"


/*********************
*      DEFINES
*********************/
#define AP_SSID "web_lvgl"
#define AP_PSSWD "password"


/**********************
 *  STATIC VARIABLES
 **********************/
const char* TAG = "main";


/**********************
 *  STATIC PROTOTYPES
 **********************/
static void wifi_setup();
static esp_err_t wifi_event_handler(void* ctx, system_event_t* event);
static void IRAM_ATTR lv_tick_task(void);


/**********************
 *   APPLICATION MAIN
 **********************/
void app_main() {
	wifi_setup();
    lv_init();

	websocket_driver_init();

    static lv_color_t* buf1;
    static lv_color_t* buf2;
    static lv_disp_buf_t disp_buf;
    
    // LVGL Display buffers
    buf1 = (lv_color_t *) malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    buf2 = (lv_color_t *) malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

	// Output
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = websocket_driver_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

	// Input
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = websocket_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);

    esp_register_freertos_tick_hook(lv_tick_task);

    demo_create();

	// Evaluate LVGL while there is something to display on
    while (1) {
    	if (websocket_driver_available()) {
        	vTaskDelay(1);
        	lv_task_handler();
        } else {
        	vTaskDelay(100 / portTICK_RATE_MS);
        }
    }
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t wifi_event_handler(void* ctx, system_event_t* event) {
	switch(event->event_id) {
		case SYSTEM_EVENT_AP_START:
			ESP_LOGI(TAG,"Access Point Started");
			break;
		case SYSTEM_EVENT_AP_STOP:
			ESP_LOGI(TAG,"Access Point Stopped");
			break;
		case SYSTEM_EVENT_AP_STACONNECTED:
			ESP_LOGI(TAG,"STA Connected, MAC=%02x:%02x:%02x:%02x:%02x:%02x AID=%i",
					 event->event_info.sta_connected.mac[0],event->event_info.sta_connected.mac[1],
					 event->event_info.sta_connected.mac[2],event->event_info.sta_connected.mac[3],
					 event->event_info.sta_connected.mac[4],event->event_info.sta_connected.mac[5],
					 event->event_info.sta_connected.aid);
			break;
		case SYSTEM_EVENT_AP_STADISCONNECTED:
			ESP_LOGI(TAG,"STA Disconnected, MAC=%02x:%02x:%02x:%02x:%02x:%02x AID=%i",
					 event->event_info.sta_disconnected.mac[0],event->event_info.sta_disconnected.mac[1],
					 event->event_info.sta_disconnected.mac[2],event->event_info.sta_disconnected.mac[3],
					 event->event_info.sta_disconnected.mac[4],event->event_info.sta_disconnected.mac[5],
					 event->event_info.sta_disconnected.aid);
			break;
		case SYSTEM_EVENT_AP_PROBEREQRECVED:
			ESP_LOGI(TAG,"AP Probe Received");
			break;
		case SYSTEM_EVENT_AP_STA_GOT_IP6:
			ESP_LOGI(TAG,"Got IP6=%01x:%01x:%01x:%01x",
					 event->event_info.got_ip6.ip6_info.ip.addr[0],event->event_info.got_ip6.ip6_info.ip.addr[1],
					 event->event_info.got_ip6.ip6_info.ip.addr[2],event->event_info.got_ip6.ip6_info.ip.addr[3]);
 			break;
		default:
			ESP_LOGI(TAG,"Unregistered event=%i",event->event_id);
			break;
	}
	return ESP_OK;
}


static void wifi_setup() {
	ESP_LOGI(TAG,"starting tcpip adapter");
	tcpip_adapter_init();
	nvs_flash_init();
	ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));

	tcpip_adapter_ip_info_t info;
	memset(&info, 0, sizeof(info));
	IP4_ADDR(&info.ip, 192, 168, 4, 1);
	IP4_ADDR(&info.gw, 192, 168, 4, 1);
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);
	ESP_LOGI(TAG,"setting gateway IP");
	ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
	
	ESP_LOGI(TAG,"starting DHCPS adapter");
	ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
	
	ESP_LOGI(TAG,"starting event loop");
	ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));

	ESP_LOGI(TAG,"initializing WiFi");
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = AP_SSID,
			.password= AP_PSSWD,
			.channel = 0,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.ssid_hidden = 0,
			.max_connection = 4,
			.beacon_interval = 100
		}
	};
	if (strlen(AP_PSSWD) == 0) {
    	wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG,"WiFi set up");
}


static void IRAM_ATTR lv_tick_task(void) {
    lv_tick_inc(portTICK_RATE_MS);
}

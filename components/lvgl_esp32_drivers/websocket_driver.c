/**
* Websocket driver for LittleVGL
*
* Contains a web server that serves a simple page to a client that draws the output
* of LVGL into a canvas in a webserver via a websocket.  Mouse/Touch actions are returned
* via the websocket for LVGL input.
*
* Networking must have been setup prior to starting this driver.
*
*/

/*********************
 *      INCLUDES
 *********************/
#include "websocket_driver.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/api.h"
#include "lvgl.h"
#include "string.h"
#include "websocket_server.h"


/**********************
 *      TYPEDEFS
 **********************/
typedef struct
{
	SemaphoreHandle_t mutex;
	uint8_t flag;
	uint16_t x;
	uint16_t y;
} pointer_t;


/**********************
 *  STATIC VARIABLES
 **********************/
static const char* TAG = "websocket_driver";
 
// Connection state
static bool websocket_connected = false;

// Inner-task queue
static QueueHandle_t client_queue;
const static int client_queue_size = 10;

// Pre-allocated pixel buffer
static uint8_t pixel_buf[DISP_BUF_SIZE * sizeof(lv_color_t) + 9];

// Current pointer value
static pointer_t pointer;


/**********************
 *  STATIC PROTOTYPES
 **********************/
static void websocket_callback(uint8_t num, WEBSOCKET_TYPE_t type, char* msg, uint64_t len);
static void http_serve(struct netconn *conn);
static void server_task(void* pvParameters);
static void server_handle_task(void* pvParameters);
 
 
/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void websocket_driver_init()
{
	ESP_LOGI(TAG, "Initialization.");
	
	ws_server_start();
	xTaskCreate(&server_task, "server_task", 3000, NULL, 9, NULL);
	xTaskCreate(&server_handle_task, "server_handle_task", 4000, NULL, 6, NULL);
	
	pointer.mutex = xSemaphoreCreateMutex();
	pointer.flag = 0;
	pointer.x = 0;
	pointer.y = 0;
}


bool websocket_driver_available()
{
	return websocket_connected;
}


void websocket_driver_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
	int i;
	uint8_t* buf;
	uint8_t pixel_depth_32;
	uint32_t size;
	
	if (websocket_connected) {
#if LV_COLOR_DEPTH == 32
		pixel_depth_32 = 1;
#else
		pixel_depth_32 = 0;
#endif

		size = lv_area_get_width(area) * lv_area_get_height(area);

		buf = pixel_buf;
		if (buf != NULL) {
			// Create a binary message containing the coordinates and 32-bit pixel
			// data.  This must match the javascript unpacking routine in index.html.
			// This way we don't have to worry about endianness.
			//
			// Load the region coordinates
			*buf++ = pixel_depth_32;
			*buf++ = (area->x1 >> 8) & 0xFF;
			*buf++ =  area->x1       & 0xFF;
			*buf++ = (area->y1 >> 8) & 0xFF;
			*buf++ =  area->y1       & 0xFF;
			*buf++ = (area->x2 >> 8) & 0xFF;
			*buf++ =  area->x2       & 0xFF;
			*buf++ = (area->y2 >> 8) & 0xFF;
			*buf++ =  area->y2       & 0xFF;
			
			if (pixel_depth_32 == 1) {
				// Load the 32-bit pixel data: RGBA8888
				lv_color32_t* p32 = (lv_color32_t*) color_map;
				for (i=0; i<size; i++) {
					*buf++ = p32[i].ch.red;
					*buf++ = p32[i].ch.green;
					*buf++ = p32[i].ch.blue;
					*buf++ = p32[i].ch.alpha;
				}
			} else {
				// Load the 16-bit pixel data: RGB565
				lv_color16_t* p16 = (lv_color16_t*) color_map;
				for (i=0; i<size; i++) {
					*buf++ = p16[i].full >> 8;
					*buf++ = p16[i].full & 0xFF;
				}
			}
			
			// Send the buffer to the web page for display
			i = ws_server_send_bin_all((char *) pixel_buf, (uint64_t) size * sizeof(lv_color_t) + 9);
		}
	}
	
	lv_disp_flush_ready(drv);
}


bool websocket_driver_read(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
	xSemaphoreTake(pointer.mutex, portMAX_DELAY);
	data->point.x = (int16_t) pointer.x;
	data->point.y = (int16_t) pointer.y;
	data->state = (pointer.flag == 0) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
	xSemaphoreGive(pointer.mutex);
	
	return false;
}


/**********************
 *   STATIC FUNCTIONS
 **********************/
// handles websocket events
void websocket_callback(uint8_t num, WEBSOCKET_TYPE_t type, char* msg, uint64_t len) {
	const static char* TAG = "websocket_callback";

	switch(type) {
		case WEBSOCKET_CONNECT:
			ESP_LOGI(TAG, "client %i connected!", num);
			websocket_connected = true;
			break;
		case WEBSOCKET_DISCONNECT_EXTERNAL:
			ESP_LOGI(TAG, "client %i sent a disconnect message", num);
			websocket_connected = false;
			break;
		case WEBSOCKET_DISCONNECT_INTERNAL:
			ESP_LOGI(TAG, "client %i was disconnected", num);
			websocket_connected = false;
			break;
		case WEBSOCKET_DISCONNECT_ERROR:
			ESP_LOGI(TAG, "client %i was disconnected due to an error", num);
			websocket_connected = false;
			break;
		case WEBSOCKET_BIN:
			if ((uint32_t) len == 5) {
				xSemaphoreTake(pointer.mutex, portMAX_DELAY);
				pointer.flag = (uint8_t) msg[0];
				pointer.x = ((uint8_t) msg[1] << 8) | (uint8_t) msg[2];
				pointer.y = ((uint8_t) msg[3] << 8) | (uint8_t) msg[4];
				xSemaphoreGive(pointer.mutex);
			}
			break;
		default:
			ESP_LOGI(TAG, "client %i send unhandled websocket type %d", num, type);
			break;
	}
}

// serves any clients
static void http_serve(struct netconn *conn) {
	const static char* TAG = "http_server";
	const static char HTML_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
	const static char ICO_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/x-icon\n\n";

	struct netbuf* inbuf;
	static char* buf;
	static uint16_t buflen;
	static err_t err;

	// default page
	extern const uint8_t index_html_start[] asm("_binary_index_html_start");
	extern const uint8_t index_html_end[] asm("_binary_index_html_end");
	const uint32_t index_html_len = index_html_end - index_html_start;
	
	// favicon.ico (automatically requested by browsers from the "root directory")
	extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
	extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
	const uint32_t favicon_ico_len = favicon_ico_end - favicon_ico_start;

	netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
	ESP_LOGI(TAG, "reading from client...");
	err = netconn_recv(conn, &inbuf);
	ESP_LOGI(TAG, "read from client");
	if(err == ERR_OK) {
		netbuf_data(inbuf, (void**)&buf, &buflen);
		if (buf) {

			// default page
			if (strstr(buf,"GET / ")
				&& !strstr(buf,"Upgrade: websocket")) {
				
				ESP_LOGI(TAG,"Sending /");
				netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, index_html_start, index_html_len, NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			// default page websocket
			else if (strstr(buf,"GET / ")
					 && strstr(buf,"Upgrade: websocket")) {
				ESP_LOGI(TAG,"Requesting websocket on /");
				ws_server_add_client(conn, buf, buflen, "/", websocket_callback);
				netbuf_delete(inbuf);
			}
			
			else if(strstr(buf,"GET /favicon.ico ")) {
				ESP_LOGI(TAG,"Sending favicon.ico");
				netconn_write(conn,ICO_HEADER,sizeof(ICO_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn,favicon_ico_start,favicon_ico_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else {
				ESP_LOGI(TAG,"Unknown request");
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}
		}
		else {
			ESP_LOGI(TAG,"Unknown request (empty?...)");
			netconn_close(conn);
			netconn_delete(conn);
			netbuf_delete(inbuf);
		}
	}
	else {
		ESP_LOGE(TAG,"error on read, closing connection");
		netconn_close(conn);
		netconn_delete(conn);
		netbuf_delete(inbuf);
	}
}

// handles clients when they first connect. passes to a queue
static void server_task(void* pvParameters) {
	const static char* TAG = "server_task";
	struct netconn *conn, *newconn;
	static err_t err;
	client_queue = xQueueCreate(client_queue_size, sizeof(struct netconn*));

	conn = netconn_new(NETCONN_TCP);
	netconn_bind(conn, NULL, 80);
	netconn_listen(conn);
	ESP_LOGI(TAG,"server listening");
	do {
		err = netconn_accept(conn, &newconn);
		ESP_LOGI(TAG, "new client");
		if(err == ERR_OK) {
			xQueueSendToBack(client_queue, &newconn, portMAX_DELAY);
			//http_serve(newconn);
		}
	} while(err == ERR_OK);
	netconn_close(conn);
	netconn_delete(conn);
	ESP_LOGE(TAG,"task ending, rebooting board");
	esp_restart();
}

// receives clients from queue, handles them
static void server_handle_task(void* pvParameters) {
	const static char* TAG = "server_handle_task";
	struct netconn* conn;
	ESP_LOGI(TAG, "task starting");
	for(;;) {
		xQueueReceive(client_queue, &conn,portMAX_DELAY);
		if (!conn) continue;
		http_serve(conn);
	}
	vTaskDelete(NULL);
}



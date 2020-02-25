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
#ifndef WEBSOCKET_DRIVER_H
#define WEBSOCKET_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl/lvgl.h"


/*********************
*      DEFINES
*********************/
#define DISP_BUF_SIZE (LV_HOR_RES_MAX * 30)


/**********************
 * GLOBAL PROTOTYPES
 **********************/
void websocket_driver_init();
bool websocket_driver_available();
void websocket_driver_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map);
bool websocket_driver_read(lv_indev_drv_t * drv, lv_indev_data_t * data);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WEBSOCKET_DRIVER_H */
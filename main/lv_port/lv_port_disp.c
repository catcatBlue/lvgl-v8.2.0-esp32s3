/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "disp_spi.h"
#include "esp_log.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#if CONFIG_USE_ESP32_DISP_DRIVERS
#include "lvgl_helpers.h"
#else
#include "bsp_lcd.h"
#endif

/*********************
 *      DEFINES
 *********************/
#define TAG "lv_port_disp"

#if CONFIG_USE_ESP32_DISP_DRIVERS
#define MY_DISP_HOR_RES 480 /* 240 */
#define MY_DISP_VER_RES 320 /* 240 */
#endif

// #define DISP_BUF_SIZE_CUSTOM (MY_DISP_HOR_RES * /* MY_DISP_VER_RES */ 34) /* 10240 */
#define DISP_BUF_SIZE_CUSTOM (16384 /* 17000 */)

/* 使用lvgl_esp32_drivers的显示驱动还是p佬的显示驱动：
    https://yuanze.wang/posts/at32f403a-esp32s3-lvgl-compare/
 */
// #define CONFIG_USE_ESP32_DISP_DRIVERS

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void disp_clear(lv_disp_drv_t *disp_drv);

/**********************
 *  STATIC VARIABLES
 **********************/
DMA_ATTR lv_color_t *buf1 = NULL;
DMA_ATTR lv_color_t *buf2 = NULL;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
#if CONFIG_USE_ESP32_DISP_DRIVERS
void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */
    /* static lv_color_t * */
    buf1 = heap_caps_malloc(DISP_BUF_SIZE_CUSTOM * sizeof(lv_color_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT /* MALLOC_CAP_DMA */);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    /* static lv_color_t * */
    buf2 = heap_caps_malloc(DISP_BUF_SIZE_CUSTOM * sizeof(lv_color_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT /* MALLOC_CAP_DMA */);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_draw_buf_t draw_buf_dsc_2;

    uint32_t size_in_px = DISP_BUF_SIZE_CUSTOM;

#if defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_IL3820 || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_JD79653A || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_UC8151D || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_SSD1306
    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;
#endif

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_draw_buf_init(&draw_buf_dsc_2, buf1, buf2, size_in_px /* DISP_BUF_SIZE_CUSTOM */);

    // /* Example for 3) also set disp_drv.full_refresh = 1 below*/
    // static lv_disp_draw_buf_t draw_buf_dsc_3;
    // static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*A screen sized buffer*/
    // static lv_color_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*Another screen sized buffer*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2, MY_DISP_VER_RES * LV_VER_RES);   /*Initialize the display buffer*/

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_driver_flush;

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    // disp_drv.full_refresh = 1;

    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
    disp_drv.set_px_cb  = disp_driver_set_px;
#endif

    disp_drv.draw_buf = &draw_buf_dsc_2;
    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Display hor size: %d, ver size: %d", LV_HOR_RES, LV_VER_RES);
    ESP_LOGI(TAG, "Display buffer size: %d", DISP_BUF_SIZE_CUSTOM /* DISP_BUF_SIZE */);
}
#else

void lv_port_disp_init(void)
{
    /* static lv_color_t * */
    buf1 = heap_caps_malloc(DISP_BUF_SIZE_CUSTOM * sizeof(lv_color_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT /* MALLOC_CAP_DMA */);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    /* static lv_color_t * */
    buf2 = heap_caps_malloc(DISP_BUF_SIZE_CUSTOM * sizeof(lv_color_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT /* MALLOC_CAP_DMA */);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    /* 初始化LCD总线与寄存器 */
    spi_device_handle_t lcd_spi = bsp_lcd_init();

    /* 向lvgl注册缓冲区 */
    static lv_disp_draw_buf_t draw_buf_dsc;  //需要全程生命周期，设置为静态变量
    // lv_disp_draw_buf_init(&draw_buf_dsc, lvgl_draw_buff1, lvgl_draw_buff2, BSP_LCD_X_PIXELS*LV_PORT_DISP_BUFFER_LINES);
    lv_disp_draw_buf_init(&draw_buf_dsc, buf1, buf2, DISP_BUF_SIZE_CUSTOM);

    /* 创建并初始化用于在lvgl中注册显示设备的结构 */
    static lv_disp_drv_t disp_drv;  //显示设备描述结构
    lv_disp_drv_init(&disp_drv);    //使用默认值初始化该结构
    /* 设置显示分辨率 */
    disp_drv.hor_res = BSP_LCD_X_PIXELS;
    disp_drv.ver_res = BSP_LCD_Y_PIXELS;
    /* 将总线句柄放入lv_disp_drv_t中用户自定义段 */
    disp_drv.user_data = lcd_spi;
    /* 设置显示函数 用于在将矩形缓冲区刷新到屏幕上 */
    disp_drv.flush_cb = disp_flush;
    /* 设置缓冲区 */
    disp_drv.draw_buf = &draw_buf_dsc;

    /* 清屏并释放信号量 */
    disp_clear(&disp_drv);

    /* 注册显示设备 */
    lv_disp_drv_register(&disp_drv);

    /* 开显示 */
    bsp_lcd_display_switch(lcd_spi, true);

    ESP_LOGI(TAG, "Display hor size: %d, ver size: %d", LV_HOR_RES, LV_VER_RES);
    ESP_LOGI(TAG, "Display buffer size: %d", DISP_BUF_SIZE_CUSTOM /* DISP_BUF_SIZE */);
}

#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}
#if !CONFIG_USE_ESP32_DISP_DRIVERS
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    /* 等待传输完成 */
    bsp_lcd_draw_rect_wait_busy(disp_drv->user_data);
    /* 通知lvgl传输已完成 */
    lv_disp_flush_ready(disp_drv);

    /* 启动新的传输 */
    bsp_lcd_draw_rect(disp_drv->user_data, area->x1, area->y1, area->x2, area->y2, (const uint8_t *)color_p);
}

static void disp_clear(lv_disp_drv_t *disp_drv)
{
    uint16_t i;
    memset(disp_drv->draw_buf->buf1, 0, disp_drv->draw_buf->size * sizeof(lv_color_t));

    for (i = 0; i < disp_drv->ver_res - LV_PORT_DISP_BUFFER_LINES; i += LV_PORT_DISP_BUFFER_LINES) {
        bsp_lcd_draw_rect(disp_drv->user_data, 0, i, disp_drv->hor_res - 1, i + LV_PORT_DISP_BUFFER_LINES - 1,
                          (const uint8_t *)disp_drv->draw_buf->buf1);
        bsp_lcd_draw_rect_wait_busy(disp_drv->user_data);
    }

    /* 最后一次清屏指令不等待结束 从而使后续的传输可以得到信号量 */
    bsp_lcd_draw_rect(disp_drv->user_data, 0, i, disp_drv->hor_res - 1, disp_drv->ver_res - 1,
                      (const uint8_t *)disp_drv->draw_buf->buf1);
}
#endif


// /*Flush the content of the internal buffer the specific area on the display
//  *You can use DMA or any hardware acceleration to do this operation in the background but
//  *'lv_disp_flush_ready()' has to be called when finished.*/
// static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
// {
//     /*The most simple case (but also the slowest) to put all pixels to the screen one-by-one*/

//     int32_t x;
//     int32_t y;
//     for(y = area->y1; y <= area->y2; y++) {
//         for(x = area->x1; x <= area->x2; x++) {
//             /*Put a pixel to the display. For example:*/
//             /*put_px(x, y, *color_p)*/
//             color_p++;
//         }
//     }

//     /*IMPORTANT!!!
//      *Inform the graphics library that you are ready with the flushing*/
//     lv_disp_flush_ready(disp_drv);
// }

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
// static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                     const lv_area_t * fill_area, lv_color_t color)
//{
//     /*It's an example code which should be done by your GPU*/
//     int32_t x, y;
//     dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//     for(y = fill_area->y1; y <= fill_area->y2; y++) {
//         for(x = fill_area->x1; x <= fill_area->x2; x++) {
//             dest_buf[x] = color;
//         }
//         dest_buf+=dest_width;    /*Go to the next line*/
//     }
// }


#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif

/**
 * LVGL Configuration for Smart Sports Watch
 * Based on lv_conf_template.h for LVGL 8.3.x
 * SquareLine Studio settings: 16-bit color, no swap
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (48 * 1024)

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 10
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM          0
#define LV_DPI_DEF              200

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_USE_LOG              0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE   0
#define LV_USE_FONT_COMPRESSED   0
#define LV_USE_FONT_SUBPX       0

/*====================
   TEXT SETTINGS
 *====================*/
#define LV_TXT_ENC              LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS      " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0

/*====================
   WIDGET USAGE
 *====================*/
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1

/*====================
   EXTRA WIDGETS
 *====================*/
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/*====================
   THEME USAGE
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_BASIC      1

#endif /* LV_CONF_H */

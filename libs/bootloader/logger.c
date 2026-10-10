#include "logger.h"
#include "main.h"

#if FREE_RTOS

#include <FreeRTOS.h>
#include <semphr.h>

static SemaphoreHandle_t logger_mutex = NULL;

#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
// #include "fdk_config.h"
#define LOGGER_EN 1
// Global variables to keep track of log files.
static int LOG_LEVEL = LOGGER_DEBUG;
// static bool write_to_file = false;

static void serial_write_func(const char *s);

static p_log_func serial_write = serial_write_func;

static void serial_write_func(const char *s)
{
    printf("%s",s);
}

static const char *log_level_strings[] = {
    "[OFF]",
    "[ERROR]",
    "[WARNING]",
    "[INFO]",
    "[DEBUG]",
    
};
// const char *colors[] = {
//     "\x1B[34m",
//     "\x1B[32m",
//     "\x1B[33m",
//     "\x1B[31m",
//     "\x1B[0m",
// };
const char *colors[] = {
    "\x1B[0m",
    "\x1B[31m",
    "\x1B[33m",
    "\x1B[32m",
    "\x1B[34m", 
    
};
static const char *banner =
    "                                                                         ******               ((((((\r\n"
    "                                                                           *****,.          ######\r\n"
    "                                                                             ,,,,,,,     ,######\r\n"
    " ###########                                                     ##     --     ,,,,,,,   #####\r\n"
    " ##          (##      ## (##########)  ##########  ###########. #####   ##       ,,,,,,,   #\r\n"
    "   -------##   ##   (##  (##       ##  ##.......#. ##       ,##  ##     ##         ,,,,,.\r\n"
    " ###########    (####    (#,       ##  ##       #. #########,##  ##     ##       ,***,..   #\r\n"
    "                 *##                               ##                          ,,,,,,*   (###(\r\n"
    "               ###                                 ##                        ,,,,,,.      ######\r\n"
    "                                                                          ......,,         ######\r\n";

char buff[4096];

void log_func(LOGGING_LEVELS level, const char *TAG, const char *frmt, ...)
{
#if LOGGER_EN
    if (LOG_LEVEL < level) return;
    if (serial_write == NULL) return;

#if FREE_RTOS
    xSemaphoreTake(logger_mutex, portMAX_DELAY);
#endif

    int len = snprintf(buff, sizeof(buff), "%s%s%s : ", colors[level], log_level_strings[level], TAG);
    if (len > 0 && len < (int)sizeof(buff)) {
        va_list argp;
        va_start(argp, frmt);
        (void)vsnprintf(buff + len, sizeof(buff) - len, frmt, argp);
        va_end(argp);
    }

    serial_write(buff);

#if FREE_RTOS
    xSemaphoreGive(logger_mutex);
#endif
#endif
}

void logger_set_level(LOGGING_LEVELS level)
{
    LOG_LEVEL = level;
}
void log_print_hex(LOGGING_LEVELS level, const char *TAG, uint8_t *hex_buff, uint16_t length)
{
#if LOGGER_EN
    if (LOG_LEVEL < level)
    {
        return;
    }
#if FREE_RTOS
    xSemaphoreTake(logger_mutex, portMAX_DELAY);
#endif
    char *format = buff;
    memset(buff, 0, 2048);
    sprintf(format, "%s%s%s : ", colors[level], log_level_strings[level], TAG);
    char hex[6];
    hex[5] = 0;
    for (uint16_t i = 0; i < length; i++)
    {
        sprintf(hex, "%02X ", hex_buff[i]);
        strcat(format, hex);
    }
    strcat(format, "\r\n");
    if (serial_write == NULL)
    {
#if FREE_RTOS
        xSemaphoreGive(logger_mutex);
#endif
        return;
    }
    serial_write(format);
#if FREE_RTOS
    xSemaphoreGive(logger_mutex);
#endif
#endif
}
void logger_init(LOGGING_LEVELS level, p_log_func p_func)
{
#if FREE_RTOS
    logger_mutex = xSemaphoreCreateMutex();
#endif
    LOG_LEVEL = level;
    serial_write = p_func;
    serial_write(colors[LOGGER_INFO]);
    serial_write("\r\n\r\n");
    serial_write(banner);
    serial_write("\r\n\r\n");
    serial_write(colors[LOGGER_INFO]);
}
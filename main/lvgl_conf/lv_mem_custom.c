/**
 * @file lv_malloc_core.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "src/stdlib/lv_mem.h"
#include "esp_heap_caps.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM



void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}


// 实现 LVGL 期望的内存核心函数
void* lv_malloc_core(size_t size) {
    return (size <= 2048) ? heap_caps_malloc(size, MALLOC_CAP_INTERNAL) : heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void lv_free_core(void* ptr) {
    heap_caps_free(ptr);
}

void* lv_realloc_core(void* ptr, size_t new_size) {
    return (new_size <= 2048) ? heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL) : heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
}


#endif 

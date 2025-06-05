#include <stdio.h>
#include "lcd_touch.h"
#include "lvgl.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "config.h"
#include "application.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"  // 添加XPT2046触摸驱动头文件
#include "esp_lcd_gc9a01.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "time_manager.h"


const char *TAG = "lcd_touch";

esp_lcd_touch_handle_t tp = NULL;

// Add at global scope
static TaskHandle_t xTouchTask = NULL;

// Add touch task function
static void touch_task(void* arg) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

// Modified ISR handler
static void IRAM_ATTR touch_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(xTouchTask) {
        vTaskNotifyGiveFromISR(xTouchTask, &xHigherPriorityTaskWoken);
    }
    if(xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}


void touch_spi_init(void) {
    // 1. Configure SPI device
    spi_device_interface_config_t touch_devcfg = {0};
    touch_devcfg.mode = 0;
    touch_devcfg.clock_speed_hz = 2*1000*1000;
    touch_devcfg.spics_io_num = TOUCH_CS_PIN;
    touch_devcfg.queue_size = 4;
    touch_devcfg.cs_ena_pretrans = 1;
    touch_devcfg.cs_ena_posttrans = 1;
    
    // Configure SPI device to use auto-acquire
    touch_devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    
    spi_device_handle_t touch_spi;
    ESP_ERROR_CHECK(spi_bus_add_device(TOUCH_SPI_HOST, &touch_devcfg, &touch_spi));

    // 2. Configure panel IO using V5.3.2 compatible API
    esp_lcd_panel_io_spi_config_t tp_io_config = {0};
    tp_io_config.dc_gpio_num = GPIO_NUM_NC;
    tp_io_config.cs_gpio_num = TOUCH_CS_PIN;
    tp_io_config.pclk_hz = 2*1000*1000;
    tp_io_config.lcd_cmd_bits = 8;
    tp_io_config.lcd_param_bits = 8;
    tp_io_config.spi_mode = 0;
    tp_io_config.trans_queue_depth = 4;


    // 3. Create panel IO handle
    esp_lcd_panel_io_handle_t tp_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_SPI_HOST, &tp_io_config, &tp_io));

    // 4. Initialize touch driver
    esp_lcd_touch_config_t touch_cfg = {};
    touch_cfg.x_max = DISPLAY_WIDTH;  // 240
    touch_cfg.y_max = DISPLAY_HEIGHT; // 320（根据屏幕型号调整）
    touch_cfg.int_gpio_num = TOUCH_IRQ_PIN;
    touch_cfg.rst_gpio_num = GPIO_NUM_NC;
    touch_cfg.flags = {
        .swap_xy = 0,
        .mirror_x = 0,
        .mirror_y = 1
    };
   
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &touch_cfg, &tp));

    // 5. Configure interrupts
    gpio_config_t irq_cfg = {
        .pin_bit_mask = BIT64(TOUCH_IRQ_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&irq_cfg));
    
    auto err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "gpio_install_isr_service success");
    }
    auto err2 = gpio_isr_handler_add(TOUCH_IRQ_PIN, touch_isr_handler, NULL);
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err2));
    } else {
        ESP_LOGI(TAG, "gpio_isr_handler_add success");
    }

    // In touch_spi_init() after interrupt config:
    xTaskCreate(touch_task, "touch", 4096, NULL, 2, &xTouchTask); // Priority 4 → 2
}

void lv_indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    gpio_set_level(TOUCH_CS_PIN, 0);
    esp_err_t read_ret = esp_lcd_touch_read_data(tp);  // 检查读取是否成功
    gpio_set_level(TOUCH_CS_PIN, 1);

    if (read_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read touch data: %s", esp_err_to_name(read_ret));
    }

    bool pressed = esp_lcd_touch_get_coordinates(tp, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);
    
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0] + 10;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "Touchpad pressed at x = %d, y = %d", (int)(data->point.x), (int)(data->point.y));
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lvgl_touch_init() { 
    #if 0 // 触摸屏使用独立的SPI
    static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = lv_obj_get_display(lv_scr_act());
    indev_drv.read_cb = lv_indev_read_cb;
    indev_drv.user_data = tp;

    lv_indev_drv_register(&indev_drv);
    #else // 触摸屏和显示使用同一个SPI
    static lv_indev_t *lvgl_touch_indev = NULL;

       /* Add touch input (for selected screen) */
    lvgl_port_touch_cfg_t touch_cfg = {};
    touch_cfg.disp = lv_obj_get_display(lv_scr_act());
    touch_cfg.handle = tp;

    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    lv_indev_set_read_cb(lvgl_touch_indev, lv_indev_read_cb);  // hook read
    #endif
    ESP_LOGI(TAG, "Register touch driver to LVGL");
}

void my_lv_log_print(lv_log_level_t level, const char * buf) {
    ESP_LOGI(TAG, "lvgl log: %s", buf);
}

SpiTouchLcdDisplay::SpiTouchLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, int offset_x, int offset_y,
    bool mirror_x, bool mirror_y, bool swap_xy,
    DisplayFonts fonts) :SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy, fonts)
{
    //lv_log_register_print_cb(my_lv_log_print);  // 设置你的打印回调函数
    // 创建显示gif表情的图片对象
    createGifEmoji();	
    // 使用gif表情，不需要使用表情图片
    /*if (emotion_label_ != nullptr) {
        lv_obj_del(emotion_label_);
        emotion_label_ = nullptr;
    }*/

    // 在状态栏下方创建一个新的标签用于显示闹钟信息
    clock_alarm_label_ = lv_label_create(container_);
    lv_obj_set_width(clock_alarm_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(clock_alarm_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(clock_alarm_label_, "");
}

SpiTouchLcdDisplay::~SpiTouchLcdDisplay() {
    if (clock_alarm_label_ != nullptr) {
        lv_obj_del(clock_alarm_label_);
        clock_alarm_label_ = nullptr;
    }
}

void SpiTouchLcdDisplay::SetClockAlarm(const char* clockAlarmInfo)
{
    DisplayLockGuard lock(this);
    if (clock_alarm_label_ == nullptr) {
        return;
    }  
    lv_label_set_text(clock_alarm_label_, clockAlarmInfo);
}

void SpiTouchLcdDisplay::createGifEmoji() {
    emoji_img = lv_gif_create(container_);
    if (emoji_img == nullptr) {
        ESP_LOGE(TAG, "Failed to create GIF object");
    }
    ESP_LOGI(TAG, "创建GIF image表情图片");

   // lv_obj_set_style_img_recolor_opa(emoji_img, LV_OPA_TRANSP, 0);
   // lv_obj_set_style_img_opa(emoji_img, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(emoji_img, LV_OPA_TRANSP, 0);
    //lv_obj_set_style_blend_mode(emoji_img, LV_BLEND_MODE_ADDITIVE, 0);

    lv_obj_align(emoji_img, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_size(emoji_img, 240, 180);
}
    
void SpiTouchLcdDisplay::SetEmotion(const char* emotion) {  
    if (this->emoji_img == nullptr) {
        ESP_LOGI(TAG, "显示表情 %s失败，图片对象未创建", emotion);
        return;
    }
    ESP_LOGI(TAG, "显示表情 %s", emotion);
    DisplayLockGuard lock(this);
    //lv_obj_add_flag(this->emoji_img, LV_OBJ_FLAG_HIDDEN);
    char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s.gif", emotion);
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        lv_gif_set_src(this->emoji_img, path);
    } else {
        ESP_LOGE(TAG, "表情文件 %s 不存在，加载默认表情", path);
        lv_gif_set_src(this->emoji_img, "/spiffs/neutral.gif");
    }
}

void SpiTouchLcdDisplay::UpdateStatusBar(bool update_all) {
    SpiLcdDisplay::UpdateStatusBar(update_all);

    static bool isEmptyDisplay = false; // 控制display->SetClockAlarm("")只执行一次

    std::string clockAlarm = TimeManager::GetInstance().CheckEvents();
    if (clockAlarm != "") {
        this->SetClockAlarm(clockAlarm.c_str());
        isEmptyDisplay = false;
    } else if (!isEmptyDisplay) {
        this->SetClockAlarm("");
        isEmptyDisplay = true;
    }
}
#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include "driver/uart.h"
#include "lcd_touch.h"
#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

// SD卡驱动添加
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include "sdmmc_cmd.h"

// spiffs文件系统
#include <esp_spiffs.h>
#include "esp_system.h"

#include "mcp_server.h"

#include "time_manager.h"

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "BoardESP32TouchSD"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);


void MountStorage() {
    // Mount the storage partition
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");
        // 获取分区使用情况
        size_t total = 0, used = 0;
        esp_spiffs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "Total: %d KB, Used: %d KB", total/1024, used/1024);        
        FILE* f = fopen("/spiffs/excited.gif", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file spiffs/excited.gif");
        } else {
            fclose(f);
            ESP_LOGI(TAG, "open /spiffs/excited.gif successfully");
        }
    }
    lv_fs_stdio_init();

    // 内部 RAM 剩余  
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);  
    // 外部 PSRAM 剩余  
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);  
    // 最大连续块  
    size_t max_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);  
    ESP_LOGI("MEM", "内部 RAM: %d KB, PSRAM: %d KB, 最大块: %d KB",  
            free_internal/1024, free_spiram/1024, max_block/1024);  
}

class BoardWifiTouchSD : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    sdmmc_card_t* sdcard;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }
    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();

        // 启动闹钟管理
        TimeManager::GetInstance().Start();
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        MountStorage();

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiTouchLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
#endif
                                    });
    }


 
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
        thing_manager.AddThing(iot::CreateThing("ClockAlarm"));
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();

        const char *clock_alarmer_desc = R"delimeter(设置闹钟，支持添加闹钟（包括一次性闹钟，每天重复、每周重复、每月重复的周期性闹钟），删除闹钟，修改闹钟时间，命令的参数如下：
    repeatType: single表示一次性闹钟,weekly表示每周重复的周期性闹钟,monthly表示每月重复的周期性闹钟,everyday表示每天重复的周期性闹钟;
    operation: add、del或modify。add表示添加闹钟；del表示删除闹钟,删除闹钟必须指定提醒的事件event，repeatDays填要删除哪几天的闹钟，没有指定则删除整个闹钟；modify表示修改闹钟，必须指定闹钟的提醒事件event，匹配event的闹钟的时间;
    time: 一次性闹钟的提醒时间格式为YYYY-MM-DD HH:mm:ss,需要转换成UTC时间；weekly、monthly、everyday闹钟为一天内的提醒时间,格式为HH:mm:ss。
    repeatDays: weekly闹钟该参数填一个星期哪几天需要提醒,以逗号分隔的json数组，如[1,2,3,4,5]表示星期一到星期五。monthly闹钟该参数填一个月哪几天需要提醒,如[1,2,3]表示1号,2号,3号需要提醒;其他类型闹钟填：无
    event: 提醒的事件;
)delimeter";

        mcp_server.AddTool("self.clock.set_alarmer", clock_alarmer_desc, PropertyList({
            Property("operation", kPropertyTypeString),
            Property("repeatType", kPropertyTypeString),
            Property("time", kPropertyTypeString),
            Property("repeatDays", kPropertyTypeString),
            Property("event", kPropertyTypeString),
        }), [this](const PropertyList& properties) -> ReturnValue {
            auto operation = properties["operation"].value<std::string>();
            auto repeatType = properties["repeatType"].value<std::string>();
            auto repeatDays = properties["repeatDays"].value<std::string>();
            auto reminderTime = properties["time"].value<std::string>();
            auto reminderEvent = properties["event"].value<std::string>();

            auto timestr = reminderTime.c_str();            
            TimeManager::GetInstance().SetClocker(operation, repeatType, repeatDays, reminderTime, reminderEvent);
            ESP_LOGI(TAG, "设置闹钟: 操作：%s, 类型：%s, 时间：%s, 重复：%s, 事件：%s", operation.c_str(), repeatType.c_str(), timestr, repeatDays.c_str(), reminderEvent.c_str());

            return true;
        });
    }

public:
    BoardWifiTouchSD() :
        boot_button_(BOOT_BUTTON_GPIO) {

        int intr_alloc_flags = 0;
        uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, intr_alloc_flags);

        InitializeSpi();
        InitializeLcdDisplay();
        // 触摸屏初始化
        touch_spi_init();
        lvgl_touch_init();
        InitializeButtons();
        InitializeSDCard(); 
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    ~BoardWifiTouchSD() {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdcard);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    void TestSdcard() { 
        // 创建测试文件
        FILE* f = fopen("/sdcard/test.txt", "w");
        if (f) {
            fprintf(f, "SDMMC 4-bit mode test\n");
            fclose(f);
        }

        // 读取目录
        DIR* dir = opendir("/sdcard");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                ESP_LOGI(TAG, "Found file: %s", entry->d_name);
            }
            closedir(dir);
        }
    }

    // 添加SD卡初始化方法
    void InitializeSDCard() {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = 20 * 1000; // 初始频率设为20MHz

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.clk = SDMMC_CLK_PIN;
        slot_config.cmd = SDMMC_CMD_PIN;
        slot_config.d0 = SDMMC_DATA0_PIN;
        slot_config.d1 = SDMMC_DATA1_PIN;
        slot_config.d2 = SDMMC_DATA2_PIN;
        slot_config.d3 = SDMMC_DATA3_PIN;
        slot_config.width = 4; // 4线模式

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
        };

        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, 
                                            &mount_config, &sdcard);

        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount filesystem");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SD card (0x%x)", ret);
            }
            return;
        } 
            
        ESP_LOGI(TAG, "SDMMC initialized ok");
        TestSdcard();        

        // 打印SD卡信息
        sdmmc_card_print_info(stdout, sdcard);
    }
};

DECLARE_BOARD(BoardWifiTouchSD);

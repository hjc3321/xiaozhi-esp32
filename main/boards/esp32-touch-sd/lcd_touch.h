#ifndef LCD_TOUDH_H
#define LCD_TOUDH_H

#include "lcd_display.h"

void touch_spi_init();
void lvgl_touch_init();

class SpiTouchLcdDisplay : public SpiLcdDisplay {
public:
    SpiTouchLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
    virtual ~SpiTouchLcdDisplay();

protected:
    void createGifEmoji();
    virtual void SetEmotion(const char* emotion);
    virtual void SetClockAlarm(const char* clockAlarmInfo);
    void UpdateStatusBar(bool update_all) override;
    lv_obj_t *clock_alarm_label_ = nullptr;
    lv_obj_t *emoji_img = nullptr; // 显示gif表情的图片对象
};

#endif /* LCD_TOUDH_H */

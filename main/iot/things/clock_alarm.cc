#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"
#include "time_manager.h"
#include <esp_log.h>
#include "application.h"
#include "assets/lang_config.h"

#define TAG "ClockAlarm"

namespace iot {

// 这里仅定义 ClockAlarm 的属性和方法，不包含具体的实现
class ClockAlarm : public Thing {
private:
   

public:
    ClockAlarm() : Thing("ClockAlarm", "闹钟") {
         // 定义设备的属性
         /*
        properties_.AddStringProperty("reminderTime", "提醒时间", [this]() -> std::string {
            return reminderTime;
        });
        properties_.AddStringProperty("reminderEvent", "提醒事件", [this]() -> std::string {
            return reminderEvent;
        });*/

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SetAlarmer", "设置闹钟", ParameterList({
            Parameter("operation", "操作类型", kValueTypeString, true), // 操作类型，"add"表示添加闹钟，"del"表示删除闹钟，"modify"表示修改闹钟"
            Parameter("repeatType", "重复类型", kValueTypeString, true), // single表示一次性闹钟,weekly表示一周内哪几天需要提醒的闹钟,monthly表示一个月内哪几天需要提醒的闹钟,everyday表示每天都需要提醒的闹钟,workday表示工作日需要提醒的闹钟"
            Parameter("time", "提醒时间", kValueTypeString, true), // 一次性闹钟的提醒时间格式为YYYY-MM-DD HH:mm:ss, weekly、monthly、everyday或workday的闹钟为一天内的提醒时间,格式为HH:mm:ss
            Parameter("repeatDays", "重复日期", kValueTypeString, false), // weekly闹钟该参数填哪一个星期哪几天需要提醒,以逗号分隔，如"1,2,3,4,5"表示星期一到星期五；monthly闹钟该参数填一个月哪几天需要提醒,如"1,2,3"表示1号,2号,3号需要提醒"，其它闹钟不需要该参数
            Parameter("event", "提醒事件", kValueTypeString, false)
        }), [this](const ParameterList& parameters) {
            auto operation = parameters["operation"].string();
            auto repeatType = parameters["repeatType"].string();
            auto repeatDays = parameters["repeatDays"].string();
            auto reminderTime = parameters["time"].string();
            auto reminderEvent = parameters["event"].string();
            auto timestr = parameters["time"].string().c_str();
            std::string eventstr = parameters["event"].string();
            
            TimeManager::GetInstance().SetClocker(operation, repeatType, repeatDays, reminderTime, eventstr);
            ESP_LOGI(TAG, "设置闹钟: 操作：%s, 类型：%s, 时间：%s, 重复：%s, 事件：%s", operation.c_str(), repeatType.c_str(), timestr, repeatDays.c_str(), reminderEvent.c_str());
        });
    }
};

} // namespace iot

DECLARE_THING(ClockAlarm);

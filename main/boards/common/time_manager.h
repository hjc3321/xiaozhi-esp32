#ifndef D1F60EF8_1B0D_412E_B1C6_A1CAB3F7F50B
#define D1F60EF8_1B0D_412E_B1C6_A1CAB3F7F50B
#ifndef B9358D60_3F64_43D5_9D3B_F81613B271DE
#define B9358D60_3F64_43D5_9D3B_F81613B271DE

#include <functional>
#include <string>
#include <list>
#include <mutex>
#include "esp_log.h"
#include "display.h"

class TimeManager {
public:
    struct TimedEvent {
        // repeatType: single表示一次性闹钟,weekly表示一周内哪几天需要提醒的闹钟,monthly表示一个月内哪几天需要提醒的闹钟,everyday表示每天都需要提醒的闹钟,workday表示工作日需要提醒的闹钟"
        std::string repeatType;
        // weekly闹钟该参数填哪一个星期哪几天需要提醒,以逗号分隔，如"1,2,3,4,5"表示星期一到星期五；monthly闹钟该参数填一个月哪几天需要提醒,如"1,2,3"表示1号,2号,3号需要提醒"，其它闹钟不需要该参数
        std::string repeatDays;
        // timeStr: 一次性闹钟的提醒时间格式为YYYY-MM-DD HH:mm:ss, weekly、monthly、everyday或workday的闹钟为一天内的提醒时间,格式为HH:mm:ss
        std::string timeStr;
        std::string event; // 闹钟事件信息

        time_t trigger_time; // 闹钟触发时间，根据repeatType、repeatDays和timeStr计算得出。对于周期性闹钟，首次添加闹钟时计算触发时间，闹钟触发时重新计算下一次触发时间。
        
        bool operator<(const TimedEvent& rhs) const {
            return trigger_time > rhs.trigger_time; // 小顶堆
        }
    };

    static TimeManager& GetInstance() {
        static TimeManager instance;
        return instance;
    }

    // 计算触发时间
    time_t GetTriggerTime(const std::string &repeatType, const std::string &repeatDays, const std::string &timeStr) const;

    /* 
    operation:操作类型，"add"表示添加闹钟，"del"表示删除闹钟，"modify"表示修改闹钟"
    repeatType: single表示一次性闹钟,weekly表示一周内哪几天需要提醒的闹钟,monthly表示一个月内哪几天需要提醒的闹钟,everyday表示每天都需要提醒的闹钟,workday表示工作日需要提醒的闹钟"
    repeatDays: weekly闹钟该参数填哪一个星期哪几天需要提醒,以逗号分隔，如"1,2,3,4,5"表示星期一到星期五；monthly闹钟该参数填一个月哪几天需要提醒,如"1,2,3"表示1号,2号,3号需要提醒"，其它闹钟不需要该参数
    time_str:单词闹钟格式为"YYYY-MM-DD HH:MM:SS", 周期性闹钟格式为"HH:MM:SS"
    */
    void SetClocker(const std::string &operation, const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event);

    std::string CheckEvents();
    bool Start();
    int GetAlarmCount();
    // 返回下一次触发的闹钟
    TimedEvent GetNextAlarm();
    std::string GetNextAlarmTime();
    std::string GetNextAlarmEvent();
    // 从nvs加载闹钟信息
    void LoadAlarmsFromFile();
    // 保存闹钟信息到nvs
    void SaveAlarmsToFile();

private:    
    std::list<TimedEvent> events_;
    std::mutex mutex_;
    const char* ALARM_FILE = "/config/clockalarms.dat";
    bool isSntpSynced = false;
    void UpdateTriggerTime();
    void AlarmCallback(const TimedEvent &clocker);
    void AddClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event);
    void DelClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event);
    void ModifyClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event);
    std::string GetDisplayInfo();
    void CheckAlarmClocks();
};


#endif /* B9358D60_3F64_43D5_9D3B_F81613B271DE */


#endif /* D1F60EF8_1B0D_412E_B1C6_A1CAB3F7F50B */

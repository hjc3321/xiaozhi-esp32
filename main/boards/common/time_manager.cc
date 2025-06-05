#include "time_manager.h"
#include "application.h"
#include <cstring>
#include <esp_log.h>
#include "esp_sntp.h"
#include "board.h"
#include "display.h"
#include <esp_netif_sntp.h>
#include "assets/lang_config.h"
#include <nvs_flash.h>
#include <nvs.h>

#define TAG "TimeManager"

inline bool IS_ONCE_CLOCK(const std::string &repeatType) {
    return repeatType == "single" || repeatType == "once";
}

inline bool IS_EVERYDAY_CLOCK(const std::string &repeatType) {
    return repeatType == "everyday" ||  repeatType == "daily";
}

std::string formatSeconds(int total_seconds);
static std::vector<int> parseDays(const std::string &daysStr);

void TimeManager::AlarmCallback(const TimedEvent &clocker) {
    auto& app = Application::GetInstance();
    ESP_LOGI(TAG, "触发闹钟:事件%s", clocker.event.c_str());
    app.PlaySound(Lang::Sounds::P3_ALARM);
    Display* display = Board::GetInstance().GetDisplay();
    if (display != nullptr) {
        std::string clockAlarmInfo = "闹钟：" + clocker.event;
        display->SetChatMessage("system", clockAlarmInfo.c_str());
    }
}

// 解析逗号分隔的数字字符串
static std::vector<int> parseDays(const std::string &daysStr) {
    std::vector<int> days;    
    std::stringstream ss(daysStr);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            days.push_back(std::stoi(item));
        }
    }
    return days;
}

// 在文件末尾添加函数实现
std::string formatSeconds(int total_seconds) {
    int days = total_seconds / 86400;
    int remaining = total_seconds % 86400;
    int hours = remaining / 3600;
    remaining = remaining % 3600;
    int minutes = remaining / 60;
    int seconds = remaining % 60;

    std::string result;
    if (days > 0) {
        result += std::to_string(days) + "天";
    }
    if (hours > 0) {
        result += std::to_string(hours) + "小时";
    }
    if (minutes > 0) {
        result += std::to_string(minutes) + "分钟";
    }
    if (seconds > 0 || (hours == 0 && minutes == 0 && days == 0)) { // 保证至少显示0秒
        result += std::to_string(seconds) + "秒";
    }
    return result;
}

// 将time_t转换为字符串格式
std::string TimeToString(time_t time) {
    struct tm timeinfo = {0};
    localtime_r(&time, &timeinfo);
    char strftime_buf[64]; 
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    return strftime_buf;
}

// 等待时间同步
static bool wait_for_time(int retry_count = 1)
{
    int retry = 0;
    bool isSynced = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
    while (!isSynced) {
        if (++retry < retry_count) {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            isSynced = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
        } else {
            break;
        }
    }

    // 获取当前时间
    time_t now;
    time(&now);
    std::string time_str = TimeToString(now);
    // 打印当前时间
    ESP_LOGI(TAG, "Current time: %s", time_str.c_str());

    return isSynced;
}

bool TimeManager::Start() {
    // 设置中国标准时区
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // Legacy SNTP configuration
    esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_init();

    // 等待时间同步
    this->isSntpSynced = wait_for_time(1);

    // 从文件加载闹钟信息
    this->LoadAlarmsFromFile();
    if (this->isSntpSynced) {
        this->UpdateTriggerTime();
    }
    return true;
}

// 计算触发时间，如果是一次性闹钟，直接以timeStr作为触发时间，如果是周期性闹钟，判断当前时间已经超过timeStr，计算下一次触发时间
time_t TimeManager::GetTriggerTime(const std::string &repeatType, const std::string &repeatDays, const std::string &timeStr) const
{
    if (!this->isSntpSynced) {
        return 0;
    }

    struct tm timeinfo = {0};
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);

    // 解析时间字符串
    if (IS_ONCE_CLOCK(repeatType)) {
        struct tm specified = {0};
        strptime(timeStr.c_str(), "%Y-%m-%d %H:%M:%S", &specified);
        return mktime(&specified);
    }
    
    // 解析周期性时间HH:mm:ss
    int hour, min, sec;
    sscanf(timeStr.c_str(), "%d:%d:%d", &hour, &min, &sec);

    // 设置基础时间
    struct tm base = *localtime(&now);
    base.tm_hour = hour;
    base.tm_min = min;
    base.tm_sec = sec;

    // 计算触发时间
    time_t trigger = mktime(&base);
    
    if (repeatType == "weekly") {
        std::vector<int> target_days = parseDays(repeatDays);
        time_t earliest = 0;
        
        // 检查本周剩余天数
        for (int i = 0; i < 7; ++i) {
            struct tm candidate = base;
            candidate.tm_mday += i;
            mktime(&candidate);
            
            if (std::find(target_days.begin(), target_days.end(), candidate.tm_wday) != target_days.end()) {
                time_t t = mktime(&candidate);
                if (t > now && (earliest == 0 || t < earliest)) {
                    earliest = t;
                }
            }
        }
        
        // 如果本周没有找到，检查下周
        if (earliest == 0) {
            for (int i = 0; i < 7; ++i) {
                struct tm candidate = base;
                candidate.tm_mday += (7 + i);
                mktime(&candidate);
                
                if (std::find(target_days.begin(), target_days.end(), candidate.tm_wday) != target_days.end()) {
                    time_t t = mktime(&candidate);
                    if (earliest == 0 || t < earliest) {
                        earliest = t;
                    }
                }
            }
        }
        trigger = earliest;
    } else if (repeatType == "monthly") {
        std::vector<int> target_days = parseDays(repeatDays);
        time_t earliest = 0;
        struct tm next_month = base;
        
        // 检查本月剩余天数
        for (auto day : target_days) {
            if (day >= base.tm_mday) {
                struct tm candidate = base;
                candidate.tm_mday = day;
                time_t t = mktime(&candidate);
                if (t > now && (earliest == 0 || t < earliest)) {
                    earliest = t;
                }
            }
        }
        
        // 如果本月没有找到，直接使用下月的target_days里面的第一天
        if (earliest == 0) {
            next_month.tm_mon++;
            next_month.tm_mday = 1;
            mktime(&next_month);
            
            struct tm candidate = next_month;
            candidate.tm_mday = target_days[0];
            earliest = mktime(&candidate);
        }
        trigger = earliest;
    } else if (IS_EVERYDAY_CLOCK(repeatType)) {
        // 设置当天具体时间
        struct tm today = *localtime(&now);
        today.tm_hour = hour;
        today.tm_min = min;
        today.tm_sec = sec;
        trigger = mktime(&today);

        // 仅当触发时间已过时才加1天
        if (trigger <= now) {
            today.tm_mday += 1;
            trigger = mktime(&today);
        }
    }

    return trigger;
}
void TimeManager::AddClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event) {
    time_t now = time(nullptr);
    time_t trigger_time = this->GetTriggerTime(repeatType, repeatDays, time_str);

    if (this->isSntpSynced) {
        // 单次闹钟触发时间已过不添加
        if (trigger_time < now && IS_ONCE_CLOCK(repeatType)) {
            ESP_LOGI(TAG, "单次闹钟[%s]已过时，不添加", event.c_str());
            return;
        }
    }

    std::string time_now_str = TimeToString(now);    
    std::string trigger_time_str = TimeToString(trigger_time);    
    ESP_LOGI(TAG, "添加[%s]闹钟[%s]，当前时间:%s, 闹钟时间：%s, 下次触发时间:%s, 重复：%s", 
        repeatType.c_str(), event.c_str(), 
        time_now_str.c_str(), time_str.c_str(), trigger_time_str.c_str(), repeatDays.c_str());
    events_.push_back({repeatType, repeatDays, time_str, event, trigger_time});
}

void TimeManager::ModifyClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event) {
    auto it = events_.begin();
    while (it != events_.end()) {
        // event和repeatType 都完全匹配
        if (it->event != event || it->repeatType != repeatType) {
            ++it;
            continue;
        }

        it->repeatDays = repeatDays;
        it->timeStr = time_str;

        it->trigger_time = this->GetTriggerTime(it->repeatType, it->repeatDays, it->timeStr);

        time_t now = time(nullptr);
        std::string time_now_str = TimeToString(now);    
        std::string trigger_time_str = TimeToString(it->trigger_time);   

        ESP_LOGI(TAG, "修改[%s]闹钟[%s]，当前时间:%s, 闹钟时间：%s, 下次触发时间:%s, 重复：%s", 
            repeatType.c_str(), event.c_str(), 
            time_now_str.c_str(), time_str.c_str(), trigger_time_str.c_str(), repeatDays.c_str());
        ++it;           
    }
    return;
}

void TimeManager::DelClocker(const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event) {
    auto it = events_.begin();
    while (it != events_.end()) {
        if ((it->event == event) && (it->repeatType == repeatType) && (it->timeStr == time_str)) {
            if (repeatDays.empty()) { // repeatDays为空则删除整个闹钟
                ESP_LOGI(TAG, "删除闹钟: %s", it->event.c_str());
                it = events_.erase(it);
                continue;
            }
            
            // 处理周期闹钟的天数更新
                
            // 解析现有天数和要删除的天数
            auto current_days = parseDays(it->repeatDays);
            auto remove_days = parseDays(repeatDays);
            
            // 计算差集
            std::vector<int> new_days;
            for (auto day : current_days) {
                if (std::find(remove_days.begin(), remove_days.end(), day) == remove_days.end()) {
                    new_days.push_back(day);
                }
            }
            
            // 生成新的repeatDays字符串
            if (!new_days.empty()) {
                std::stringstream ss;
                for (size_t i = 0; i < new_days.size(); ++i) {
                    if (i > 0) ss << ",";
                    ss << new_days[i];
                }
                it->repeatDays = ss.str();
                it->trigger_time = this->GetTriggerTime(it->repeatType, it->repeatDays, it->timeStr);
                ESP_LOGI(TAG, "更新闹钟周期: %s -> %s", repeatDays.c_str(), it->repeatDays.c_str());
                ++it;
                continue;
            }
            
            
            // 如果天数差集为空或直接删除
            ESP_LOGI(TAG, "删除闹钟: %s (类型:%s)", it->event.c_str(), it->repeatType.c_str());
            it = events_.erase(it);
        } else {
            ++it;
        }
    }
    return;
}

void TimeManager::SetClocker(const std::string &operation, const std::string &repeatType, const std::string &repeatDays, const std::string &time_str, const std::string &event) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (operation == "add" or operation == "set") {
        AddClocker(repeatType, repeatDays, time_str, event);
    } else if (operation == "del") {
        DelClocker(repeatType, repeatDays, time_str, event);
    } else if (operation == "modify") {
        ModifyClocker(repeatType, repeatDays, time_str, event);
    } else {
        ESP_LOGI(TAG, "不支持的闹钟操作: %s", operation.c_str());
    }
    
    SaveAlarmsToFile(); // 添加保存调用
}

void TimeManager::UpdateTriggerTime() {
    auto eventIter = events_.begin();
    time_t now = time(nullptr);
    bool updated = false;

    while (eventIter != events_.end()) {
        eventIter->trigger_time = this->GetTriggerTime(eventIter->repeatType, eventIter->repeatDays, eventIter->timeStr);
        ESP_LOGI(TAG, "更新闹钟[%s]，当前时间:%s, 闹钟时间：%s, 下次触发时间:%s, 重复：%s", 
            eventIter->event.c_str(), TimeToString(now).c_str(), eventIter->timeStr.c_str(), TimeToString(eventIter->trigger_time).c_str(), eventIter->repeatDays.c_str());

        if (IS_ONCE_CLOCK(eventIter->repeatType) && eventIter->trigger_time <= now) {     
            ESP_LOGI(TAG, "删除过期闹钟[%s]，当前时间:%s, 闹钟时间：%s, 下次触发时间:%s, 重复：%s", 
                eventIter->event.c_str(), TimeToString(now).c_str(), eventIter->timeStr.c_str(), TimeToString(eventIter->trigger_time).c_str(), eventIter->repeatDays.c_str());
            eventIter = events_.erase(eventIter);   
            updated = true;        
        } else {
            ++eventIter;
        }
    }
    if (updated) {
        SaveAlarmsToFile(); // 添加保存调用
    }
}

void TimeManager::CheckAlarmClocks() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (events_.empty()) {
        return;
    }    

    if (!this->isSntpSynced) {
        this->isSntpSynced = wait_for_time(0);
        if (!this->isSntpSynced) {
            return;
        } else {
            ESP_LOGI(TAG, "SNTP同步成功，更新闹钟触发时间");
            this->UpdateTriggerTime();
        }
    }
    
    time_t now = time(nullptr);
    auto eventIter = events_.begin();
    while (eventIter != events_.end()) {
        auto &event = *eventIter;
        if (now >= event.trigger_time) {
            Application::GetInstance().Schedule([this, event](){
                this->AlarmCallback(event);
            });
            if (IS_ONCE_CLOCK(event.repeatType)) { // 一次性闹钟删除掉
                ESP_LOGI(TAG, "删除一次性闹钟: %s (原始数量:%d)", eventIter->event.c_str(), events_.size());
                eventIter = events_.erase(eventIter);              
                continue;
            } else {
                auto new_trigger = this->GetTriggerTime(event.repeatType, event.repeatDays, event.timeStr);
                eventIter->trigger_time = new_trigger;
                std::string time_now_str = TimeToString(now);
                std::string trigger_time_str = TimeToString(eventIter->trigger_time);
                ESP_LOGI(TAG, "更新闹钟: %s, 当前时间：%s, 下次触发时间:%s", event.event.c_str(), time_now_str.c_str(), trigger_time_str.c_str());
                ++eventIter;
            }
        } else {
            ++eventIter;
        }
    }
}
std::string TimeManager::CheckEvents() {    
    CheckAlarmClocks();
    return this->GetDisplayInfo();
}

int TimeManager::GetAlarmCount() {
    return events_.size();
}

// 返回下一次触发的闹钟
TimeManager::TimedEvent TimeManager::GetNextAlarm() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (events_.empty()) {
        return {};  // Return empty object
    }

    auto earliest = events_.begin();
    for (auto it = events_.begin(); it != events_.end(); ++it) {
        if (it->trigger_time < earliest->trigger_time) {
            earliest = it;  // Just update iterator position
        }
    }
    return *earliest;  // Return copy of the element
}

std::string TimeManager::GetNextAlarmTime() {
    if (events_.empty()) {
        return "";
    }
    time_t now = time(nullptr);
    auto event = this->GetNextAlarm();
    int secs = event.trigger_time - now;
    return formatSeconds(secs);    
}

std::string TimeManager::GetNextAlarmEvent() {
    if (events_.empty()) {
        return "";
    }
    auto event = this->GetNextAlarm();
    return event.event;
}

std::string TimeManager::GetDisplayInfo() {
    if (events_.empty()) {
        return "";
    }

    int alarm_count = events_.size();

    if (!this->isSntpSynced) {
        std::string alarm_info = std::to_string(alarm_count) + "个闹钟, " + "正在校准时间";
        return alarm_info;

    } else {
        time_t now = time(nullptr);
        auto event = this->GetNextAlarm();
        int secs = event.trigger_time - now;  
        std::string alarm_info = std::to_string(alarm_count) + "个闹钟    " + formatSeconds(secs) + "后需要" + event.event;
        return alarm_info;
    }  
}

void TimeManager::SaveAlarmsToFile() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("clock_alarm", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    // 将events_序列化为字符串
    std::string data;
    for (const auto& event : events_) {
        data += event.repeatType + "|" + event.repeatDays + "|" + event.timeStr + "|" + event.event + "\n";
    }

    // 写入NVS
    err = nvs_set_str(my_handle, "alarms", data.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alarms to NVS (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved %d alarms to NVS", events_.size());
    }

    nvs_commit(my_handle);
    nvs_close(my_handle);
}

void TimeManager::LoadAlarmsFromFile() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("clock_alarm", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    // 从NVS读取数据
    char* data = NULL;
    size_t required_size = 0;
    err = nvs_get_str(my_handle, "alarms", data, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No alarm data found in NVS");
        nvs_close(my_handle);
        return;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read alarms from NVS (%s)", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    // 解析数据
    data = new char[required_size];
    err = nvs_get_str(my_handle, "alarms", data, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read alarms from NVS (%s)", esp_err_to_name(err));
        delete[] data;
        nvs_close(my_handle);
        return;
    }

    std::istringstream stream(data);
    delete[] data;
    nvs_close(my_handle);

    events_.clear();
    std::string line;
    while (std::getline(stream, line)) {
        auto pos1 = line.find('|');
        auto pos2 = line.find('|', pos1 + 1);
        auto pos3 = line.find('|', pos2 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) continue;

        TimedEvent event;
        event.repeatType = line.substr(0, pos1);
        event.repeatDays = line.substr(pos1 + 1, pos2 - pos1 - 1);
        event.timeStr = line.substr(pos2 + 1, pos3 - pos2 - 1);
        event.event = line.substr(pos3 + 1);
        events_.push_back(event);
    }
}
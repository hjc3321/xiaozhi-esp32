
#include "esp_http_client.h"
#include "esp_log.h"
#include "web_search.h"
#include "html.hpp"
#include <vector>
#include <list>

typedef struct {
    std::string title;
    std::string url;
    std::string content;
} SearchResult;

// 这些网站获取不到内容，从搜索结果过滤
const char *EXCLUDE_URLS[] = {"https://www.zhihu.com", "https://zhuanlan.zhihu.com"};

// https证书
extern const char server_cert_pem_start[] asm("_binary_cacert_pem_start");
const char *global_ca_store = server_cert_pem_start;

#define MAX_RESULTS 5
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.0.0"

static const char *TAG = "BING_SEARCH";


std::string g_http_response = "";

bool is_excluded_url(const char *url) {
    for (int i = 0; i < sizeof(EXCLUDE_URLS) / sizeof(EXCLUDE_URLS[0]); i++) {
        if (strstr(url, EXCLUDE_URLS[i]) != NULL) {
            return true;
        }
    }
    return false;
}

std::string url_encode(const char *str) {
    const char *hex = "0123456789ABCDEF";
    size_t len = strlen(str);
    std::string encoded;

    for (size_t i = 0; i < len; i++) {
        if (isalnum((unsigned char)str[i]) || 
            strchr("-_!~*'().", str[i]) != NULL) {
            encoded += str[i];
        } else {
            encoded += '%';
            encoded += hex[(str[i] >> 4) & 0xF];
            encoded += hex[str[i] & 0xF];
        }
    }
    return encoded;
}

esp_err_t bing_event_handler(esp_http_client_event_t *evt) { 
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG, "bing HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            g_http_response += std::string((const char *)(evt->data), evt->data_len);
            break;

        default:
            break;
    }
    return ESP_OK;
}

std::string web_get_content(const char *url) {
    esp_http_client_config_t config = {0};
    config.url = url;
    config.cert_pem = global_ca_store;
    config.skip_cert_common_name_check = false;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size = 4096;
    config.buffer_size_tx = 1024;
    config.method = HTTP_METHOD_GET;
    config.user_agent = USER_AGENT;
    config.timeout_ms = 1000;    
    config.event_handler = bing_event_handler;
    g_http_response = "";
    ESP_LOGI(TAG, "HTTP Request URL: %s", url);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "HTTP Request Successful");
            return g_http_response;
        } else {
            ESP_LOGE(TAG, "HTTP Status: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP Request Failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return "";
}

std::string parse_html_content_by_keys(html::node_ptr &node, const char *keys[], int keyNum) {
    std::string content = "";
    for (int i = 0; i < keyNum; i++) {
        std::vector<html::node*> selected = node->select(keys[i]);
        if (!selected.empty()) {
            for (auto elem :selected) {
                content += elem->to_text();
            }
        }
    }   
    return content;
}

std::string parse_html_content(const char *html) {
    html::parser p;
    html::node_ptr node = p.parse(html);
    std::string content = "";
    const char *contentKey[] = {"article", "div.content", "div.article", "div#content", "div#article"}; 
    content = parse_html_content_by_keys(node, contentKey, sizeof(contentKey) / sizeof(contentKey[0]));
    if (!content.empty()) {
        return content;
    }

    // 提取失败将整个html转换为文本
    const char *all_text_keys[] = {"div", "span"};
    content = parse_html_content_by_keys(node, all_text_keys, sizeof(all_text_keys) / sizeof(all_text_keys[0]));
    if (!content.empty()) {
        return content;
    }
    return content;
}

int bing_parse_result(const char *html, std::list<SearchResult> &results, int max_results) {
    html::parser p;
    html::node_ptr node = p.parse(html);
    std::vector<html::node*> selected = node->select("li.b_algo");
    for(auto elem : selected) {
        if (results.size() >= max_results) {
            break;
        }
        auto hrefNodes = elem->select("h2 a");
        if (hrefNodes.empty()) {
            continue;
        }
        auto hrefNode = hrefNodes[0];
        std::string title = hrefNode->to_text();
        std::string url = hrefNode->get_attr("href");
        ESP_LOGI(TAG, "bing result: %s %s", url.c_str(), title.c_str());
        if (is_excluded_url(url.c_str())) {
            ESP_LOGI(TAG, "ignore excluded url: %s", url.c_str());
            continue;
        }
        std::string rawHtmlContent = web_get_content(url.c_str());
        if (rawHtmlContent.empty()) {
            continue;
        }
        std::string parsedContent = parse_html_content(rawHtmlContent.c_str());      
        SearchResult result;
        result.title = title;
        result.content = parsedContent.empty() ? rawHtmlContent : parsedContent;
        result.url = url;
        // ESP_LOGI(TAG, "parsed content: %s", parsedContent.c_str());
        results.push_back(result);        
    }
        
    return results.size();
}

int bing_search(const char *keyword, std::list<SearchResult> &results, int max_results) {
    // 构建请求URL
    char url[256];
    int retcode = -1;
    snprintf(url, sizeof(url), "https://cn.bing.com/search?q=%s", url_encode(keyword).c_str());
    g_http_response = "";
    // 执行HTTP请求
    esp_http_client_config_t config = {0};
    config.url = url;
    config.cert_pem = global_ca_store;
    config.skip_cert_common_name_check = false;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size = 4096;
    config.buffer_size_tx = 1024;
    config.method = HTTP_METHOD_GET;
    config.user_agent = USER_AGENT;
    config.timeout_ms = 1000;    
    config.event_handler = bing_event_handler;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "HTTP Request Successful");
            // ESP_LOGI(TAG, "HTTP Response: %s", g_http_response.c_str());
            retcode = bing_parse_result(g_http_response.c_str(), results, max_results);
        } else {
            ESP_LOGE(TAG, "HTTP Status: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP Request Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return retcode;
}
int web_search(const char *keyword, std::string &result, int max_results) {
    std::list<SearchResult> results;
    bing_search(keyword, results, max_results);
    for (auto item : results) {
        result += item.title + "\n";
        result += item.content + "\n";
        result += item.url + "\n";
    }
    return results.size();
}
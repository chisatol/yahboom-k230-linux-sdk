#include <cstring>
#include "llm_interface.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048

std::string g_url="";
std::string g_key="";

typedef struct {
    char *memory;
    size_t size;
} ResponseBuffer;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *mem = (ResponseBuffer *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        fprintf(stderr, "Not enough memory for response buffer\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';
    return realsize;
}

// 解析 JSON 并提取 url 和 key
int parseConfig(std::string& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        printf("Failed to open file: ",filePath);
        return 0;
    }

    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    // 使用 cJSON 解析 JSON 字符串
    cJSON* root = cJSON_Parse(content.str().c_str());
    if (!root) {
        printf("Failed to parse JSON content");
        return 0;
    }

    // 提取 url 字段
    cJSON* url = cJSON_GetObjectItem(root, "url");
    if (!url || url->type != cJSON_String) {
        cJSON_Delete(root);
        printf("Missing or invalid 'url' field in JSON");
        return 0;
    }
    g_url = url->valuestring; // 将 url 的值赋给 g_url

    // 提取 key 字段
    cJSON* key = cJSON_GetObjectItem(root, "key");
    if (!key || key->type != cJSON_String) {
        cJSON_Delete(root);
        printf("Missing or invalid 'key' field in JSON");
        return 0;
    }
    g_key = key->valuestring; // 将 key 的值赋给 g_key
    // 释放 JSON 对象
    cJSON_Delete(root);
    return 1;
}

int send_to_openai(char *offer, char *answer){
    CURL *curl;
    CURLcode res;
    ResponseBuffer response;
    response.memory = (char *)malloc(1); // Initial memory allocation
    response.size = 0;
    std::string config_path="config.json";
    int ret=parseConfig(config_path);
    if(!ret){
        return 0;
    }else{
        printf("url:%s\n key: %s\n",g_url.c_str(),g_key.c_str());
    }

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        char auth_header[MAX_HTTP_OUTPUT_BUFFER];

        // Create the Authorization header
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_key.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/sdp");
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, g_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, offer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return 0;
        } else {
            printf("Respose SDP:\n%s\n", response.memory);
            strncpy(answer, response.memory, MAX_HTTP_OUTPUT_BUFFER - 1);
        }

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return 1;
    }
    else{
        printf("%s\n", "create curl instance failed!");
        return 0;
    }
}
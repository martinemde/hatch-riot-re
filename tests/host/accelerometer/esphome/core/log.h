#pragma once
#include <cstdio>
#define ESP_LOGI(tag, ...) do { (void)(tag); if (false) std::printf(__VA_ARGS__); } while(0)
#define ESP_LOGV ESP_LOGI
#define ESP_LOGCONFIG ESP_LOGI

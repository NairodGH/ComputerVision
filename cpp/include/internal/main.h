#pragma once

#include <jni.h>
#include <android/log.h>
#include <android/asset_manager_jni.h>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstring>
#include <utility>
#include <regex>
#include "net.h"
#include "cpu.h"

#define PRINTF(...) __android_log_print(ANDROID_LOG_DEBUG, "", __VA_ARGS__)

using namespace std;
using namespace ncnn;
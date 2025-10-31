#include "CacheBase.h"
#include <functional>

std::function<void(int)> g_on_evict_key = nullptr;

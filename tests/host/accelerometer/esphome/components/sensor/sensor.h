#pragma once
#include <cmath>
namespace esphome::sensor { class Sensor { public: float state=NAN; void publish_state(float v){state=v;} }; }

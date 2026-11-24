#pragma once
namespace esphome::binary_sensor { class BinarySensor { public: bool state=false, valid=false; void publish_state(bool v){state=v; valid=true;} void invalidate_state(){valid=false;} }; }

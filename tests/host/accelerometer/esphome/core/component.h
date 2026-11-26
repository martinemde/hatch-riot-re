#pragma once
namespace esphome { class PollingComponent { public: virtual ~PollingComponent()=default; virtual void update(){}; virtual void dump_config(){}; virtual void on_shutdown(){}; }; }

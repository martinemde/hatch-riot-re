#pragma once
#include "hatch_scenes.h"
#include "esphome/core/automation.h"

namespace esphome::hatch_scenes {

template<typename... Ts> class SaveAction : public Action<Ts...>, public Parented<HatchScenes> {
 public:
  TEMPLATABLE_VALUE(int, slot)
  void play(const Ts &...x) override {
    if (this->slot_.has_value()) {
      this->parent_->save(this->slot_.value(x...));
    } else {
      this->parent_->save();
    }
  }
};

template<typename... Ts> class LoadAction : public Action<Ts...>, public Parented<HatchScenes> {
 public:
  TEMPLATABLE_VALUE(int, slot)
  void play(const Ts &...x) override { this->parent_->load(this->slot_.value(x...)); }
};

template<typename... Ts> class NextAction : public Action<Ts...>, public Parented<HatchScenes> {
 public:
  void play(const Ts &...x) override { this->parent_->next(); }
};

template<typename... Ts> class PreviousAction : public Action<Ts...>, public Parented<HatchScenes> {
 public:
  void play(const Ts &...x) override { this->parent_->previous(); }
};

template<typename... Ts> class DeleteAction : public Action<Ts...>, public Parented<HatchScenes> {
 public:
  TEMPLATABLE_VALUE(int, slot)
  void play(const Ts &...x) override {
    if (this->slot_.has_value()) {
      this->parent_->remove(this->slot_.value(x...));
    } else {
      this->parent_->remove();
    }
  }
};

}  // namespace esphome::hatch_scenes

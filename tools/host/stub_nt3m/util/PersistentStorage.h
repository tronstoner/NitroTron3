#pragma once
// Host stub of daisy::PersistentStorage: RAM only, starts blank (FACTORY), like
// a pedal whose QSPI settings sector has never been written.
#include "daisy.h"

namespace daisy {

template <typename SettingStruct>
class PersistentStorage {
 public:
  enum class State { UNKNOWN = 0, FACTORY = 1, USER = 2 };
  explicit PersistentStorage(QSPIHandle&) {}
  void Init(const SettingStruct& defaults, uint32_t = 0) {
    defaults_ = defaults;
    settings_ = defaults;
    state_    = State::FACTORY;
  }
  State GetState() const { return state_; }
  SettingStruct& GetSettings() { return settings_; }
  void Save() { state_ = State::USER; }
  void RestoreDefaults() { settings_ = defaults_; state_ = State::FACTORY; }

 private:
  SettingStruct defaults_{}, settings_{};
  State state_ = State::UNKNOWN;
};

}  // namespace daisy

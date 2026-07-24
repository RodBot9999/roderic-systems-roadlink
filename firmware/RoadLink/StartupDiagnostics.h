#pragma once

#include <Arduino.h>

enum class ModuleId : uint8_t {
  Can,
  Gps,
  Sim,
  Storage,
  Display,
  Unknown
};

inline const char* moduleName(ModuleId module)
{
  switch (module) {
    case ModuleId::Can:     return "CAN";
    case ModuleId::Gps:     return "GPS";
    case ModuleId::Sim:     return "SIM";
    case ModuleId::Storage: return "STORAGE";
    case ModuleId::Display: return "DISPLAY";
    default:                return "UNKNOWN";
  }
}

struct ModuleError {
  ModuleId module = ModuleId::Unknown;
  bool warning = false;
  String summary;
  int32_t primaryCode = 0;
  int32_t secondaryCode = 0;
};

class StartupDiagnostics {
public:
  static constexpr uint8_t MAX_ERRORS = 8;

  void clear()
  {
    errorCount_ = 0;
    overridden_ = false;
  }

  bool report(
      ModuleId module,
      const String& summary,
      int32_t primaryCode = 0,
      int32_t secondaryCode = 0,
      bool warning = false)
  {
    // Update an existing error instead of duplicating it.
    for (uint8_t i = 0; i < errorCount_; ++i) {
      if (errors_[i].module == module) {
        errors_[i].summary = summary;
        errors_[i].primaryCode = primaryCode;
        errors_[i].secondaryCode = secondaryCode;
        errors_[i].warning = warning;
        return true;
      }
    }

    if (errorCount_ >= MAX_ERRORS) {
      return false;
    }

    errors_[errorCount_].module = module;
    errors_[errorCount_].summary = summary;
    errors_[errorCount_].primaryCode = primaryCode;
    errors_[errorCount_].secondaryCode = secondaryCode;
    errors_[errorCount_].warning = warning;

    errorCount_++;
    return true;
  }

  bool reportWarning(
      ModuleId module,
      const String& summary,
      int32_t primaryCode = 0,
      int32_t secondaryCode = 0)
  {
    return report(module, summary, primaryCode, secondaryCode, true);
  }

  bool hasWarnings() const
  {
    for (uint8_t i = 0; i < errorCount_; ++i) {
      if (errors_[i].warning) return true;
    }
    return false;
  }

  bool hasFatalErrors() const
  {
    for (uint8_t i = 0; i < errorCount_; ++i) {
      if (!errors_[i].warning) return true;
    }
    return false;
  }

  bool hasErrors() const
  {
    return errorCount_ > 0;
  }

  uint8_t count() const
  {
    return errorCount_;
  }

  const ModuleError* error(uint8_t index) const
  {
    if (index >= errorCount_) {
      return nullptr;
    }

    return &errors_[index];
  }

  void overrideErrors()
  {
    overridden_ = true;
  }

  bool overridden() const
  {
    return overridden_;
  }

private:
  ModuleError errors_[MAX_ERRORS];
  uint8_t errorCount_ = 0;
  bool overridden_ = false;
};

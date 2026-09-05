#pragma once

#include "win32/app_settings.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace win32mqtt {

// Persist user intent, even while the live connection is still unsubscribing.
inline AppSettings PersistentSettings(AppSettings settings) {
    auto& records = settings.subscriptions;
    records.erase(std::remove_if(records.begin(), records.end(),
        [](const auto& record) { return record.removing; }), records.end());
    return settings;
}

inline bool SameSettings(const AppSettings& a, const AppSettings& b) {
    return a.language == b.language && a.server_uri == b.server_uri &&
        a.client_id == b.client_id && a.window_width == b.window_width &&
        a.window_height == b.window_height &&
        a.subscription_panel_width == b.subscription_panel_width && a.subscriptions.size() == b.subscriptions.size() &&
        std::equal(a.subscriptions.begin(), a.subscriptions.end(), b.subscriptions.begin(),
            [](const auto& x, const auto& y) { return x.topic == y.topic && x.active == y.active; });
}

class SettingsAutosave {
public:
    enum class Result { Idle, Saved, Failed };
    bool Pending() const { return pending_.has_value(); }
    void Schedule(AppSettings settings, std::uint64_t now, std::uint64_t delay) {
        settings = PersistentSettings(std::move(settings));
        if (saved_ && SameSettings(*saved_, settings)) { pending_.reset(); return; }
        // Unrelated notifications must not postpone an existing save or retry.
        if (pending_ && SameSettings(*pending_, settings)) {
            if (delay == 0) due_ = now;
            return;
        }
        pending_ = std::move(settings);
        due_ = now + delay;
    }
    template<class Save> Result Poll(std::uint64_t now, Save save, bool force = false) {
        if (!pending_ || (!force && now < due_)) return Result::Idle;
        if (!save(*pending_)) { due_ = now + 5000; return Result::Failed; }
        saved_ = std::move(pending_);
        pending_.reset();
        return Result::Saved;
    }
private:
    std::optional<AppSettings> saved_, pending_;
    std::uint64_t due_ = 0;
};

} // namespace win32mqtt

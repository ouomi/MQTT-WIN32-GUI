#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace win32mqtt {

struct SubscriptionRecord {
    std::wstring topic;
    bool active{false};
};

class SubscriptionCatalog {
public:
    std::size_t Size() const noexcept;
    const SubscriptionRecord* At(std::size_t index) const noexcept;
    std::size_t Find(const std::wstring& topic) const noexcept;
    bool Add(std::wstring topic);
    bool Remove(std::size_t index);
    bool SetActive(std::size_t index, bool active);
    void Replace(std::vector<SubscriptionRecord> records);
    std::vector<SubscriptionRecord> Snapshot() const;
    std::vector<std::wstring> ActiveTopics() const;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

private:
    std::vector<SubscriptionRecord> records_;
};

} // namespace win32mqtt

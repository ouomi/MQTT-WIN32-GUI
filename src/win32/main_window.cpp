#include "main_window.h"
#include "../mqtt/mqtt_topic.hpp"
#include "../settings_autosave.hpp"
#include "../settings_document.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "connection_panel.h"
#include "control_helpers.h"
#include "message_panel.h"
#include "mqtt_window_bridge.h"
#include "publish_panel.h"
#include "subscription_panel.h"
#include "ui_ids.h"
#include "will_panel.h"

namespace win32mqtt {
namespace {

constexpr wchar_t kWindowClass[] = L"WIN32-MQTT.MainWindow";
constexpr int kControlGap = 8;
constexpr int kControlMargin = 12;
constexpr int kClassicPanelInset = 4;
constexpr int kConnectionContentHeight = 54;
constexpr int kMinimumPanelWidth = 300;
constexpr int kPanelGap = 16;
constexpr int kPublishContentHeight = 126;
constexpr int kSplitterWidth = 8;
constexpr int kMinimumWindowWidth = 800;
constexpr int kMinimumWindowHeight = 600;

struct PanelBounds {
    RECT connection{};
    RECT subscriptions{};
    RECT splitter{};
    RECT messages{};
    RECT publisher{};
};

RECT InsetPanel(RECT bounds) {
    InflateRect(&bounds, -kClassicPanelInset, -kClassicPanelInset);
    return bounds;
}

int SubscriptionPanelWidth(int requested_width, int content_width) {
    const int available_width = content_width - kSplitterWidth;
    const int default_width = available_width / 2;
    if (available_width <= 2 * kMinimumPanelWidth) {
        return default_width;
    }
    return std::clamp(requested_width > 0 ? requested_width : default_width,
                      kMinimumPanelWidth, available_width - kMinimumPanelWidth);
}

PanelBounds CalculatePanelBounds(HWND status, int width, int height,
                                 int requested_subscription_width) {
    RECT status_rect{};
    GetWindowRect(status, &status_rect);
    const int status_height = status_rect.bottom - status_rect.top;
    const int client_height = height - status_height;
    const int content_width = width - 2 * kControlMargin;
    const int left_width = SubscriptionPanelWidth(requested_subscription_width, content_width);
    const int splitter_left = kControlMargin + left_width;
    const int right_x = splitter_left + kSplitterWidth;
    const int publisher_height = kPublishContentHeight + 2 * kClassicPanelInset;
    const int panel_top = kControlMargin + kConnectionContentHeight +
                          2 * kClassicPanelInset + kPanelGap;

    PanelBounds panels{};
    panels.connection = {
        kControlMargin,
        kControlMargin,
        width - kControlMargin,
        kControlMargin + kConnectionContentHeight + 2 * kClassicPanelInset,
    };
    const int work_area_bottom = client_height - kControlMargin;
    panels.subscriptions = {kControlMargin, panel_top, splitter_left, work_area_bottom};
    panels.splitter = {splitter_left, panel_top, right_x, work_area_bottom};
    panels.publisher = {right_x, work_area_bottom - publisher_height,
                        width - kControlMargin, work_area_bottom};
    panels.messages = {right_x, panel_top, width - kControlMargin,
                       panels.publisher.top - kControlGap};
    return panels;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (length == 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length == 0) {
        return L"<binary; see HEX> " + MqttMessageStore::Hex(std::string(value.substr(0, 128)));
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

UiText ConnectionStatusText(MqttConnectionState state) {
    switch (state) {
    case MqttConnectionState::Connected: return UiText::ConnectedStatus;
    case MqttConnectionState::Connecting: return UiText::ConnectingStatus;
    case MqttConnectionState::Disconnecting: return UiText::DisconnectingStatus;
    case MqttConnectionState::Failed: return UiText::ConnectionFailedStatus;
    case MqttConnectionState::Disconnected: return UiText::DisconnectedStatus;
    }
    return UiText::DisconnectedStatus;
}

MqttPublishQos ToMqttPublishQos(PublishQos qos) {
    switch (qos) {
    case PublishQos::Qos0: return MqttPublishQos::Qos0;
    case PublishQos::Qos1: return MqttPublishQos::Qos1;
    case PublishQos::Qos2: return MqttPublishQos::Qos2;
    }
    return MqttPublishQos::Qos0;
}

int RestoredWindowDimension(int saved_dimension, int minimum_dimension) {
    return saved_dimension >= minimum_dimension ? saved_dimension : minimum_dimension;
}

} // namespace

// Owns window-level state and coordinates the otherwise independent Win32 panels.
struct MainWindow::Impl {
    explicit Impl(AppSettings initial_settings)
        : settings(std::move(initial_settings)), language(settings.language),
          subscription_panel_width(settings.subscription_panel_width) {}

    void CaptureNormalWindowSize() {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        RECT bounds{};
        if (GetWindowPlacement(window, &placement)) {
            bounds = placement.rcNormalPosition;
        } else {
            GetWindowRect(window, &bounds);
        }

        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width > 0 && height > 0) {
            settings.window_width = width;
            settings.window_height = height;
        }
    }

    AppSettings CurrentSettings() {
        CaptureNormalWindowSize();
        // Never persist an incomplete edit that strict startup validation would reject.
        const auto server_uri = connection.ServerUri();
        const auto client_id = connection.ClientId();
        if (ValidSettingsServerUri(server_uri)) settings.server_uri = server_uri;
        if (ValidSettingsText(client_id)) settings.client_id = client_id;
        return {language, settings.server_uri, settings.client_id, subscriptions.Snapshot(),
                settings.window_width, settings.window_height, subscription_panel_width};
    }

    void ScheduleSettingsSave(std::uint64_t delay = 0) {
        if (!controls_ready) return;
        autosave.Schedule(CurrentSettings(), GetTickCount64(), delay);
        if (delay == 0) SavePendingSettings();
    }

    void SavePendingSettings(bool closing = false) {
        // Defer all writes while dragging and through the release debounce period.
        if (!closing && (splitter_dragging || GetTickCount64() < splitter_save_due)) return;
        const auto result = autosave.Poll(GetTickCount64(),
            [](const AppSettings& snapshot) { return SaveAppSettings(snapshot); }, closing);
        if (result == SettingsAutosave::Result::Idle) {
            if (save_failed && !autosave.Pending()) {
                save_failed = false;
                if (!closing) UpdateConnectionUi();
            }
            return;
        }
        if (result == SettingsAutosave::Result::Failed) {
            const wchar_t* detail = language == AppLanguage::Chinese
                ? L"配置保存失败，原配置已保留。改动仍在内存中，将自动重试。请检查程序目录的写入权限和磁盘空间。"
                : L"Settings could not be saved. Previous settings were preserved. Changes remain in memory and will be retried. Check directory permissions and disk space.";
            if (closing) {
                MessageBoxW(window, language == AppLanguage::Chinese
                    ? L"配置保存失败，最新改动未能保存。原配置已保留。请检查程序目录的写入权限和磁盘空间。"
                    : L"Settings could not be saved. Latest changes were not saved; previous settings were preserved. Check directory permissions and disk space.",
                    L"WIN32 MQTT", MB_OK | MB_ICONERROR);
            } else if (!save_failed) {
                messages.Append(detail);
            }
            save_failed = true;
        } else {
            if (save_failed && !closing) messages.Append(language == AppLanguage::Chinese
                ? L"配置已成功保存。" : L"Settings saved successfully.");
            save_failed = false;
        }
        if (!closing) UpdateConnectionUi();
    }

    void CreateControls() {
        connection.Create(window, language, connection_state, settings.server_uri,
                          settings.client_id);
        subscriptions.Create(window, language, settings.subscriptions);
        messages.Create(window, language);
        publisher.Create(window, language);
        publisher.SetTopics(subscriptions.ActiveTopics());
        status = AddControl(STATUSCLASSNAMEW, SBARS_SIZEGRIP | WS_CLIPSIBLINGS, IDC_STATUS,
                            window);
        will.Create(window, language, [this] {
            if (connection_state != MqttConnectionState::Connected || !will.Settings().enabled) return;
            if (ReportAdmission(mqtt->SimulateAbnormalDisconnect())) {
                connection_state = MqttConnectionState::Disconnecting;
                UpdateConnectionUi();
            }
        });
        messages.Append(std::wstring(Text(language, UiText::InitialMessage)));
        UpdateConnectionUi();
    }

    void UpdateConnectionUi() const {
        connection.UpdateText(language, connection_state);
        will.UpdateText(language, connection_state);
        publisher.SetConnected(connection_state == MqttConnectionState::Connected);
        const wchar_t* status_text = save_failed
            ? (language == AppLanguage::Chinese ? L"配置未保存，正在重试" : L"Settings not saved; retrying")
            : Text(language, ConnectionStatusText(connection_state)).data();
        SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(status_text));
    }

    void Layout(int width, int height, bool splitter_only = false) const {
        if (!splitter_only) SendMessageW(status, WM_SIZE, 0, 0);
        const PanelBounds panels = CalculatePanelBounds(status, width, height,
                                                        subscription_panel_width);
        {
            ControlLayoutBatch geometry;
            if (!splitter_only) connection.Layout(InsetPanel(panels.connection));
            subscriptions.Layout(InsetPanel(panels.subscriptions));
            messages.Layout(InsetPanel(panels.messages));
            will.Layout(publisher.Layout(InsetPanel(panels.publisher)));
        } // Commit all changed control positions before repainting.
        // Child invalidation is handled by their geometry changes. Allow paint
        // requests to coalesce instead of erasing all controls on every move.
        const RECT work_area{panels.subscriptions.left, panels.subscriptions.top,
                             panels.publisher.right, panels.subscriptions.bottom};
        RedrawWindow(window, splitter_only ? &work_area : nullptr, nullptr,
                     RDW_INVALIDATE | RDW_NOCHILDREN);
    }

    PanelBounds CurrentPanelBounds() const {
        RECT client_rect{};
        GetClientRect(window, &client_rect);
        return CalculatePanelBounds(status, client_rect.right, client_rect.bottom,
                                    subscription_panel_width);
    }

    bool IsOverSplitter(POINT point) const {
        const PanelBounds panels = CurrentPanelBounds();
        return PtInRect(&panels.splitter, point) != FALSE;
    }

    void BeginSplitterDrag(int x) {
        const PanelBounds panels = CurrentPanelBounds();
        splitter_drag_offset = x - panels.splitter.left;
        splitter_dragging = true;
        last_splitter_x = x;
        SetCapture(window);
    }

    void MoveSplitter(int x) {
        if (!splitter_dragging) {
            return;
        }
        RECT client_rect{};
        GetClientRect(window, &client_rect);
        const int content_width = client_rect.right - 2 * kControlMargin;
        const int next_width = SubscriptionPanelWidth(
            x - kControlMargin - splitter_drag_offset, content_width);
        const int current_width = SubscriptionPanelWidth(subscription_panel_width, content_width);
        if (next_width == current_width) return;
        subscription_panel_width = next_width;
        Layout(client_rect.right, client_rect.bottom, true);
    }

    void QueueSplitterMove(int x) {
        // Layout/capture can generate mouse messages without physical movement.
        if (x == last_splitter_x) return;
        last_splitter_x = x;
        pending_splitter_x = x;
        if (!splitter_update_pending) {
            splitter_update_pending = SetTimer(window, SplitterTimer, 16, nullptr) != 0;
            if (!splitter_update_pending) MoveSplitter(x);
        }
    }

    void FlushSplitterMove() {
        KillTimer(window, SplitterTimer);
        if (!splitter_update_pending) return;
        splitter_update_pending = false;
        MoveSplitter(pending_splitter_x);
    }

    void EndSplitterDrag(int x) {
        KillTimer(window, SplitterTimer);
        splitter_update_pending = false;
        MoveSplitter(x);
        CancelSplitterDrag();
        if (GetCapture() == window) {
            ReleaseCapture();
        }
    }

    void CancelSplitterDrag() {
        const bool was_dragging = splitter_dragging;
        KillTimer(window, SplitterTimer);
        splitter_update_pending = false;
        splitter_dragging = false;
        if (was_dragging) {
            splitter_save_due = GetTickCount64() + 500;
            ScheduleSettingsSave(500);
        }
    }

    void PaintClassicPanels(HDC device_context) const {
        PanelBounds panels = CurrentPanelBounds();
        DrawEdge(device_context, &panels.connection, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        DrawEdge(device_context, &panels.subscriptions, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        DrawEdge(device_context, &panels.splitter, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        DrawEdge(device_context, &panels.messages, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        DrawEdge(device_context, &panels.publisher, EDGE_RAISED, BF_RECT | BF_MIDDLE);
    }

    bool ReportAdmission(MqttAdmission result) {
        if (result == MqttAdmission::Accepted) return true;
        UiText text = UiText::MqttSessionStopped;
        if (result == MqttAdmission::TooLarge) text = UiText::MqttRequestTooLarge;
        else if (result == MqttAdmission::QueueFull) text = UiText::MqttCommandQueueFull;
        messages.Append(std::wstring(Text(language, text)));
        return false;
    }

    void ApplySubscriptionChanges(SubscriptionPanelChanges changes) {
        std::vector<std::string> desired;
        for (const auto& topic : subscriptions.ActiveTopics()) desired.push_back(WideToUtf8(topic));
        ReportAdmission(mqtt->SetSubscriptions(std::move(desired)));
        for (const std::wstring& message : changes.messages) {
            messages.Append(message);
        }
        if (changes.active_topics_changed) {
            publisher.SetTopics(subscriptions.ActiveTopics());
        }
        ScheduleSettingsSave();
    }

    void HandleConnectionRequest(const ConnectionPanelRequest& request) {
        if (request.type == ConnectionPanelRequest::Type::Disconnect) {
            mqtt->Disconnect();
            connection_state = MqttConnectionState::Disconnecting;
            UpdateConnectionUi();
            return;
        }
        if (request.type != ConnectionPanelRequest::Type::Connect) {
            return;
        }

        const std::string server_uri = WideToUtf8(request.server_uri);
        const MqttEndpointParseResult parsed = ParseMqttEndpoint(server_uri);
        if (!parsed.Succeeded()) {
            const std::wstring detail = Utf8ToWide(MqttEndpointErrorMessage(parsed.error));
            const std::wstring message = std::wstring(Text(language, UiText::InvalidServerUri)) +
                                         L"\r\n\r\n" + detail;
            ShowClassicMessageBox(window, message.c_str(),
                                  Text(language, UiText::ApplicationTitle).data(),
                                  MB_OK | MB_ICONINFORMATION);
            return;
        }

        const LastWillSettings will_settings = will.Settings();
        std::optional<MqttLastWill> last_will;
        if (will_settings.enabled) {
            const std::string will_topic = WideToUtf8(will_settings.topic);
            if (will_topic.empty()) {
                ShowClassicMessageBox(window, Text(language, UiText::LastWillTopicRequired).data(),
                                      Text(language, UiText::ApplicationTitle).data(),
                                      MB_OK | MB_ICONINFORMATION);
                return;
            }
            if (!IsValidPublishTopic(will_settings.topic)) {
                ShowClassicMessageBox(window, Text(language, UiText::InvalidPublishTopic).data(),
                                      Text(language, UiText::ApplicationTitle).data(), MB_OK | MB_ICONINFORMATION);
                return;
            }
            last_will = MqttLastWill{will_topic, WideToUtf8(will_settings.payload)};
        }

        if (ReportAdmission(mqtt->Connect(parsed.endpoint, WideToUtf8(request.client_id), std::move(last_will)))) {
            connection_state = MqttConnectionState::Connecting;
            UpdateConnectionUi();
        }
    }

    void HandlePublishRequest(const PublishPanelRequest& request) {
        if (!IsValidPublishTopic(request.topic)) {
            return;
        }
        if (connection_state != MqttConnectionState::Connected) {
            messages.Append(std::wstring(Text(language, UiText::PublishUnavailable)));
            return;
        }
        ReportAdmission(mqtt->Publish(WideToUtf8(request.topic), WideToUtf8(request.payload),
                                     ToMqttPublishQos(request.qos)));
    }

    void PollMqttEvents() {
        // Query durable state independently of the lossy message/log mailbox.
        const auto statuses = mqtt->Subscriptions();
        for (const auto& record : subscriptions.Snapshot()) {
            const auto topic = WideToUtf8(record.topic);
            const auto found = std::find_if(statuses.begin(), statuses.end(), [&](const auto& r) { return r.topic == topic; });
            std::wstring text(Text(language, UiText::SubscriptionInactive));
            bool absent = true;
            if (found != statuses.end()) {
                absent = !found->actual && !found->pending && !found->desired;
                text = !found->error.empty()
                    ? LocalizeSubscriptionDetail(language, Utf8ToWide(found->error))
                    : std::wstring(Text(language, found->pending ? UiText::SubscriptionPending
                        : found->actual ? UiText::SubscriptionConfirmed
                        : found->desired ? UiText::SubscriptionWaiting : UiText::SubscriptionInactive));
                text += L" [" + std::to_wstring(found->generation) + L":" + std::to_wstring(found->operation) + L"]";
            }
            subscriptions.UpdateStatus(record.topic, text, absent);
        }
        auto batch = mqtt_events.Take();
        for (auto& result : mqtt->TakePublishResults()) batch.events.push_back(std::move(result));
        messages.BeginBatch();
        if (batch.dropped) messages.Append(std::wstring(Text(language, UiText::MqttEventsDropped)) +
                                          std::to_wstring(batch.dropped));
        for (const auto& event : batch.events) HandleMqttEvent(event);
        messages.EndBatch();
    }

    void HandleMqttEvent(const MqttEvent& value) {
        const MqttEvent* event = &value;
        if (event->type == MqttEventType::StateChanged) {
            connection_state = event->connection_state;
            UpdateConnectionUi();
            switch (event->connection_state) {
            case MqttConnectionState::Connecting:
                messages.Append(std::wstring(Text(language, UiText::ConnectionRequested)) +
                                Utf8ToWide(event->detail));
                break;
            case MqttConnectionState::Connected:
                messages.Append(std::wstring(Text(language, UiText::ConnectedStatus)));
                ApplySubscriptionChanges({});
                break;
            case MqttConnectionState::Failed:
                messages.Append(LocalizeSubscriptionDetail(language, Utf8ToWide(event->detail)),
                                L"generation=" + std::to_wstring(event->generation) +
                                L" operation=" + std::to_wstring(event->operation));
                break;
            case MqttConnectionState::Disconnected:
                if (event->simulated_disconnect) {
                    will.ShowTestCompleted();
                    messages.Append(std::wstring(Text(language, UiText::LastWillTestCompleted)));
                } else {
                    messages.Append(std::wstring(Text(language, UiText::DisconnectedMessage)));
                }
                if (!event->detail.empty()) messages.Append(LocalizeSubscriptionDetail(language, Utf8ToWide(event->detail)),
                                L"generation=" + std::to_wstring(event->generation) +
                                L" operation=" + std::to_wstring(event->operation));
                break;
            case MqttConnectionState::Disconnecting:
                break;
            }
        } else if (event->type == MqttEventType::MessageReceived) {
            messages.Receive(*event, Utf8ToWide(event->topic), Utf8ToWide(event->payload));
        } else if (event->type == MqttEventType::PublishQueued) {
            messages.Publish(*event, Utf8ToWide(event->topic), Utf8ToWide(event->payload));
        } else if (event->type == MqttEventType::PublishRejected) {
            messages.Publish(*event, Utf8ToWide(event->topic), Utf8ToWide(event->detail));
        } else {
            messages.Append(LocalizeSubscriptionDetail(language, Utf8ToWide(event->detail)),
                                L"generation=" + std::to_wstring(event->generation) +
                                L" operation=" + std::to_wstring(event->operation));
        }
    }

    SettingsAutosave autosave;
    bool controls_ready = false;
    bool resizing = false;
    bool save_failed = false;
    HWND window{};
    ConnectionPanel connection;
    SubscriptionPanel subscriptions;
    MessagePanel messages;
    PublishPanel publisher;
    WillPanel will;
    HWND status{};
    AppSettings settings;
    AppLanguage language;
    MqttConnectionState connection_state{MqttConnectionState::Disconnected};
    int subscription_panel_width{};
    std::uint64_t splitter_save_due{};
    bool splitter_dragging{};
    bool splitter_update_pending{};
    int pending_splitter_x{};
    int last_splitter_x{};
    int splitter_drag_offset{};
    MqttWindowBridge mqtt_events;
    std::unique_ptr<MqttSession> mqtt;
    static constexpr UINT_PTR MqttEventTimer = 1;
    static constexpr UINT_PTR SplitterTimer = 2;
};

MainWindow::MainWindow(AppSettings settings)
    : impl_(std::make_unique<Impl>(std::move(settings))) {}
MainWindow::~MainWindow() = default;

bool MainWindow::Register(HINSTANCE instance) const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    window_class.lpszClassName = kWindowClass;
    window_class.lpfnWndProc = WindowProc;
    return RegisterClassExW(&window_class) != 0;
}

bool MainWindow::Create(HINSTANCE instance) {
    const int width = RestoredWindowDimension(impl_->settings.window_width,
                                              kMinimumWindowWidth);
    const int height = RestoredWindowDimension(impl_->settings.window_height,
                                               kMinimumWindowHeight);
    impl_->window = CreateWindowExW(
        0, kWindowClass, Text(impl_->language, UiText::ApplicationTitle).data(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, instance, this);
    return impl_->window != nullptr;
}

HWND MainWindow::Handle() const noexcept {
    return impl_->window;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wparam,
                                        LPARAM lparam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->impl_->window = window;
        // Default nonclient creation initializes the caption from CREATESTRUCTW.
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (self == nullptr) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    Impl& app = *self->impl_;
    switch (message) {
    case WM_CREATE:
        UseClassicWindowFrame(window);
        app.CreateControls();
        if (!SetTimer(window, Impl::MqttEventTimer, 50, nullptr)) return -1;
        app.mqtt = std::make_unique<MqttSession>(app.mqtt_events.Handler());
        app.controls_ready = true;
        app.ScheduleSettingsSave(500);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* min_max = reinterpret_cast<MINMAXINFO*>(lparam);
        min_max->ptMinTrackSize.x = kMinimumWindowWidth;
        min_max->ptMinTrackSize.y = kMinimumWindowHeight;
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        app.resizing = true;
        return 0;
    case WM_EXITSIZEMOVE:
        app.resizing = false;
        app.ScheduleSettingsSave();
        return 0;
    case WM_SIZE:
        app.Layout(LOWORD(lparam), HIWORD(lparam));
        if (!app.resizing && wparam != SIZE_MINIMIZED) app.ScheduleSettingsSave(500);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            if (app.IsOverSplitter(point)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (app.IsOverSplitter(point)) {
            app.BeginSplitterDrag(point.x);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (app.splitter_dragging) {
            app.QueueSplitterMove(GET_X_LPARAM(lparam));
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (app.splitter_dragging) {
            app.EndSplitterDrag(GET_X_LPARAM(lparam));
            return 0;
        }
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        app.CancelSplitterDrag();
        return 0;
    case WM_ERASEBKGND:
        // Paint the background with the panels, avoiding a separate blank frame.
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window, &paint);
        FillRect(device_context, &paint.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        app.PaintClassicPanels(device_context);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC device_context = reinterpret_cast<HDC>(wparam);
        // Read-only EDIT controls send WM_CTLCOLORSTATIC too. Painting their
        // text transparently can leave old glyphs behind when the log scrolls.
        if (GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) == IDC_MESSAGES) {
            SetTextColor(device_context, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(device_context, GetSysColor(COLOR_WINDOW));
            SetBkMode(device_context, OPAQUE);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        SetTextColor(device_context, GetSysColor(COLOR_BTNTEXT));
        SetBkMode(device_context, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC device_context = reinterpret_cast<HDC>(wparam);
        SetTextColor(device_context, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(device_context, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_TIMER:
        if (wparam == Impl::SplitterTimer) {
            app.FlushSplitterMove();
            return 0;
        }
        if (wparam == Impl::MqttEventTimer) {
            app.PollMqttEvents();
            app.SavePendingSettings();
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        if (app.subscriptions.HandleContextMenu(window, app.language,
                                                reinterpret_cast<HWND>(wparam), lparam)) {
            return 0;
        }
        break;
    case WM_NOTIFY: {
        if (!app.controls_ready) break;
        SubscriptionPanelChanges changes = app.subscriptions.HandleNotification(
            app.language, *reinterpret_cast<const NMHDR*>(lparam));
        if (changes.handled) {
            app.ApplySubscriptionChanges(std::move(changes));
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const WORD id = LOWORD(wparam);
        const WORD notification = HIWORD(wparam);
        if ((id == IDC_SERVER_URI || id == IDC_CLIENT_ID) && notification == EN_CHANGE) {
            app.ScheduleSettingsSave(500);
            return 0;
        }
        ConnectionPanelRequest connection_request;
        if (app.connection.HandleCommand(id, notification, app.connection_state,
                                         connection_request)) {
            app.HandleConnectionRequest(connection_request);
            return 0;
        }

        if (app.will.HandleCommand(id, notification)) return 0;

        SubscriptionPanelChanges subscription_changes =
            app.subscriptions.HandleCommand(window, app.language, id, notification);
        if (subscription_changes.handled) {
            app.ApplySubscriptionChanges(std::move(subscription_changes));
            return 0;
        }
        if (app.messages.HandleCommand(id, notification)) {
            return 0;
        }

        PublishPanelRequest publish_request;
        if (app.publisher.HandleCommand(window, app.language, id, notification,
                                        publish_request)) {
            app.HandlePublishRequest(publish_request);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        app.CancelSplitterDrag();
        KillTimer(window, Impl::MqttEventTimer);
        // Child controls are still alive here; capture text before DestroyWindow.
        if (app.controls_ready) {
            app.autosave.Schedule(app.CurrentSettings(), GetTickCount64(), 0);
            app.SavePendingSettings(true);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        app.controls_ready = false;
        KillTimer(window, Impl::MqttEventTimer);
        app.mqtt_events.Close();
        if (app.mqtt) {
            app.mqtt->Stop();
            app.mqtt.reset();
        }
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        app.window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace win32mqtt

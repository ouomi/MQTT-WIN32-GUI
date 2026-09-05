#include "localization.h"

#include <windows.h>

namespace win32mqtt {

std::wstring LocalizeSubscriptionDetail(AppLanguage language, std::wstring_view detail) {
    if (language != AppLanguage::Chinese) return std::wstring(detail);
    if (detail == L"subscription request failed") return L"订阅请求失败";
    if (detail == L"Broker rejected subscription") return L"服务器拒绝了订阅";
    struct Translation { std::wstring_view suffix; std::wstring_view translated; };
    static constexpr Translation translations[] = {
        {L": unsubscription acknowledged by broker", L"：服务器已确认退订"},
        {L": subscription rejected by broker", L"：服务器拒绝了订阅"},
        {L": subscription accepted by broker", L"：服务器已接受订阅"},
        {L": subscription accepted by broker (QoS 0)", L"：服务器已接受订阅（QoS 0）"},
        {L": subscription accepted by broker (QoS 1)", L"：服务器已接受订阅（QoS 1）"},
        {L": subscription accepted by broker (QoS 2)", L"：服务器已接受订阅（QoS 2）"},
    };
    // Match only complete protocol-generated suffixes, preserving the topic verbatim.
    for (const auto& translation : translations) {
        if (detail.size() >= translation.suffix.size()) {
            const auto start = detail.size() - translation.suffix.size();
            if (detail.substr(start) == translation.suffix) {
                return std::wstring(detail.substr(0, start)) + std::wstring(translation.translated);
            }
        }
    }
    return std::wstring(detail);
}

AppLanguage DefaultAppLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE
               ? AppLanguage::Chinese
               : AppLanguage::English;
}

std::wstring_view Text(AppLanguage language, UiText text) {
    if (language == AppLanguage::Chinese) {
        switch (text) {
        case UiText::ApplicationTitle: return L"Win32 MQTT 客户端";
        case UiText::WindowClassRegistrationFailed: return L"无法注册主窗口类。";
        case UiText::WindowCreationFailed: return L"无法创建主窗口。";
        case UiText::LanguageChinese: return L"中文";
        case UiText::LanguageEnglish: return L"English";
        case UiText::ServerUri: return L"服务器地址：";
        case UiText::ClientId: return L"客户端 ID：";
        case UiText::LastWill: return L"遗嘱消息";
        case UiText::LastWillEnabled: return L"已启用";
        case UiText::EnableLastWill: return L"启用遗嘱消息";
        case UiText::LastWillTopic: return L"遗嘱主题：";
        case UiText::LastWillPayload: return L"遗嘱内容：";
        case UiText::LastWillHint: return L"连接时提交给服务器，异常掉线后由服务器发送。连接期间无法修改配置。";
        case UiText::LastWillTest: return L"模拟异常掉线";
        case UiText::LastWillTestHint: return L"将中断当前连接。请先用另一个客户端订阅遗嘱主题，观察是否收到消息。";
        case UiText::LastWillTestCompleted: return L"已断开 TCP 连接，模拟异常掉线。";
        case UiText::LastWillTopicRequired: return L"启用遗嘱消息时，请输入遗嘱主题。";
        case UiText::Connect: return L"连接";
        case UiText::Disconnect: return L"断开连接";
        case UiText::ConnectedStatus: return L"已连接";
        case UiText::ConnectingStatus: return L"正在连接…";
        case UiText::DisconnectingStatus: return L"正在断开…";
        case UiText::ConnectionFailedStatus: return L"连接失败";
        case UiText::DisconnectedStatus: return L"未连接";
        case UiText::InvalidServerUri: return L"请输入有效的 MQTT 服务器地址（仅包含主机和可选端口）。";
        case UiText::ConnectionRequested: return L"已请求连接到 ";
        case UiText::NetworkingUnavailable: return L"。此原型尚未接入 MQTT 网络。";
        case UiText::DisconnectedMessage: return L"已断开连接。";
        case UiText::InitialMessage: return L"添加主题后，勾选复选框以启用订阅。";
        case UiText::NewTopic: return L"新主题：";
        case UiText::AddSubscription: return L"添加订阅";
        case UiText::Topic: return L"主题";
        case UiText::EnterTopicFirst: return L"请先输入主题。";
        case UiText::DuplicateSubscription: return L"该订阅已在列表中。";
        case UiText::AddSubscriptionFailed: return L"无法添加订阅。";
        case UiText::AddedSubscription: return L"已添加订阅：";
        case UiText::RemovedSubscription: return L"已移除订阅：";
        case UiText::SubscriptionActivated: return L"订阅已启用。";
        case UiText::SubscriptionDeactivated: return L"订阅已停用。";
        case UiText::DeleteSelectedSubscriptions: return L"删除选中的订阅";
        case UiText::SubscriptionMessages: return L"活动订阅的消息";
        case UiText::SubscriptionSyncStatus: return L"同步状态";
        case UiText::SubscriptionInactive: return L"未订阅";
        case UiText::SubscriptionPending: return L"等待确认";
        case UiText::SubscriptionConfirmed: return L"已确认";
        case UiText::SubscriptionWaiting: return L"等待同步";
        case UiText::SubscriptionRemoving: return L"等待退订";
        case UiText::ClearMessages: return L"清空消息";
        case UiText::Message: return L"消息：";
        case UiText::DefaultPayload: return L"来自传统 Win32 界面的问候";
        case UiText::PublishTo: return L"发布到：";
        case UiText::PublishQos: return L"QoS：";
        case UiText::Publish: return L"发布";
        case UiText::MqttRequestTooLarge: return L"操作被拒绝：发送报文上限为 4096 字节（含主题、内容和协议头）；连接主机名上限为 1024 字节。";
        case UiText::MqttCommandQueueFull: return L"操作被拒绝：命令队列已满（256 条或 1 MiB），请稍后重试。";
        case UiText::MqttEventsDropped: return L"事件队列超限，丢弃普通事件数：";
        case UiText::MqttSessionStopped: return L"操作被拒绝：会话已停止。";
        case UiText::PublishUnavailable: return L"未发布：当前未连接到 Broker。";
        case UiText::PublishQueued: return L"已加入本地发送队列：";
        case UiText::PublishRejected: return L"未发布：";
        case UiText::SelectPublishTopic: return L"请输入发布主题，也可以从候选列表选择。";
        case UiText::InvalidPublishTopic: return L"请输入有效的发布主题：不能包含 +、# 或空字符，UTF-8 编码不能超过 65535 字节。";
        case UiText::InvalidSubscriptionFilter: return L"请输入有效的订阅过滤器：+ 必须独占一层，# 必须独占最后一层；不能包含空字符，UTF-8 编码不能超过 65535 字节。";
        }
    }

    switch (text) {
    case UiText::ApplicationTitle: return L"Win32 MQTT Client";
    case UiText::WindowClassRegistrationFailed: return L"Unable to register the main window class.";
    case UiText::WindowCreationFailed: return L"Unable to create the main window.";
    case UiText::LanguageChinese: return L"中文";
    case UiText::LanguageEnglish: return L"English";
    case UiText::ServerUri: return L"Server URI:";
    case UiText::ClientId: return L"Client ID:";
    case UiText::LastWill: return L"Last Will";
    case UiText::LastWillEnabled: return L"enabled";
    case UiText::EnableLastWill: return L"Enable Last Will";
    case UiText::LastWillTopic: return L"Last Will topic:";
    case UiText::LastWillPayload: return L"Last Will message:";
    case UiText::LastWillHint: return L"Sent to the server when connecting; published by the server after an abnormal disconnect. Settings are locked while connected.";
    case UiText::LastWillTest: return L"Simulate abnormal disconnect";
    case UiText::LastWillTestHint: return L"This closes the current connection. First subscribe to the Last Will topic in another client to observe the message.";
    case UiText::LastWillTestCompleted: return L"TCP connection closed to simulate an abnormal disconnect.";
    case UiText::LastWillTopicRequired: return L"Enter a Last Will topic when Last Will is enabled.";
    case UiText::Connect: return L"Connect";
    case UiText::Disconnect: return L"Disconnect";
    case UiText::ConnectedStatus: return L"Connected";
    case UiText::ConnectingStatus: return L"Connecting…";
    case UiText::DisconnectingStatus: return L"Disconnecting…";
    case UiText::ConnectionFailedStatus: return L"Connection failed";
    case UiText::DisconnectedStatus: return L"Disconnected";
    case UiText::InvalidServerUri: return L"Enter a valid MQTT server URI containing only a host and optional port.";
    case UiText::ConnectionRequested: return L"Connection requested for ";
    case UiText::NetworkingUnavailable: return L". MQTT networking is not connected in this prototype.";
    case UiText::DisconnectedMessage: return L"Disconnected.";
    case UiText::InitialMessage: return L"Add a topic, then tick its checkbox to activate the subscription.";
    case UiText::NewTopic: return L"New topic:";
    case UiText::AddSubscription: return L"Add SUB";
    case UiText::Topic: return L"Topic";
    case UiText::EnterTopicFirst: return L"Enter a topic first.";
    case UiText::DuplicateSubscription: return L"That subscription is already in the list.";
    case UiText::AddSubscriptionFailed: return L"Unable to add the subscription.";
    case UiText::AddedSubscription: return L"Added subscription: ";
    case UiText::RemovedSubscription: return L"Removed subscription: ";
    case UiText::SubscriptionActivated: return L"Subscription activated.";
    case UiText::SubscriptionDeactivated: return L"Subscription deactivated.";
    case UiText::DeleteSelectedSubscriptions: return L"Delete selected subscription(s)";
    case UiText::SubscriptionMessages: return L"Messages from active subscriptions";
    case UiText::SubscriptionSyncStatus: return L"Sync status";
    case UiText::SubscriptionInactive: return L"Inactive";
    case UiText::SubscriptionPending: return L"Pending";
    case UiText::SubscriptionConfirmed: return L"Subscribed";
    case UiText::SubscriptionWaiting: return L"Waiting";
    case UiText::SubscriptionRemoving: return L"Removing";
    case UiText::ClearMessages: return L"Clear MSG";
    case UiText::Message: return L"Message:";
    case UiText::DefaultPayload: return L"Hello from the traditional Win32 UI";
    case UiText::PublishTo: return L"Publish to:";
    case UiText::PublishQos: return L"QoS:";
    case UiText::Publish: return L"Publish";
    case UiText::MqttRequestTooLarge: return L"Request rejected: outgoing packet limit is 4096 bytes including topic, payload and headers; connection host limit is 1024 bytes.";
    case UiText::MqttCommandQueueFull: return L"Request rejected: command queue full (256 commands or 1 MiB); retry later.";
    case UiText::MqttEventsDropped: return L"Event queue limit exceeded; ordinary events dropped: ";
    case UiText::MqttSessionStopped: return L"Request rejected: session stopped.";
    case UiText::PublishUnavailable: return L"Not published: not connected to the broker.";
    case UiText::PublishQueued: return L"Added to the local send queue: ";
    case UiText::PublishRejected: return L"Not published: ";
    case UiText::SelectPublishTopic: return L"Enter a publish topic or choose a suggestion.";
    case UiText::InvalidPublishTopic: return L"Enter a valid publish topic: no +, # or null characters; UTF-8 length must not exceed 65535 bytes.";
    case UiText::InvalidSubscriptionFilter: return L"Enter a valid filter: + must occupy a whole level, # must occupy the final level; no null characters; UTF-8 length must not exceed 65535 bytes.";
    }

    return L"";
}

} // namespace win32mqtt

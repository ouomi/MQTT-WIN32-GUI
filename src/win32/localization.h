#pragma once

#include <string>
#include <string_view>

namespace win32mqtt {

enum class AppLanguage {
    Chinese,
    English,
};

enum class UiText {
    ApplicationTitle,
    WindowClassRegistrationFailed,
    WindowCreationFailed,
    LanguageChinese,
    LanguageEnglish,
    ServerUri,
    ClientId,
    LastWill,
    LastWillEnabled,
    EnableLastWill,
    LastWillTopic,
    LastWillPayload,
    LastWillTopicRequired,
    LastWillHint,
    LastWillTest,
    LastWillTestHint,
    LastWillTestCompleted,
    Connect,
    Disconnect,
    ConnectedStatus,
    ConnectingStatus,
    DisconnectingStatus,
    ConnectionFailedStatus,
    DisconnectedStatus,
    InvalidServerUri,
    ConnectionRequested,
    NetworkingUnavailable,
    DisconnectedMessage,
    InitialMessage,
    NewTopic,
    AddSubscription,
    Topic,
    EnterTopicFirst,
    DuplicateSubscription,
    AddSubscriptionFailed,
    AddedSubscription,
    RemovedSubscription,
    SubscriptionActivated,
    SubscriptionDeactivated,
    DeleteSelectedSubscriptions,
    SubscriptionMessages,
    ClearMessages,
    SubscriptionSyncStatus,
    SubscriptionInactive,
    SubscriptionPending,
    SubscriptionConfirmed,
    SubscriptionWaiting,
    SubscriptionRemoving,
    Message,
    DefaultPayload,
    PublishTo,
    PublishQos,
    Publish,
    PublishUnavailable,
    PublishQueued,
    PublishRejected,
    SelectPublishTopic,
    InvalidPublishTopic,
    InvalidSubscriptionFilter,
    MqttRequestTooLarge,
    MqttCommandQueueFull,
    MqttSessionStopped,
    MqttEventsDropped,
};

std::wstring LocalizeSubscriptionDetail(AppLanguage language, std::wstring_view detail);

AppLanguage DefaultAppLanguage();
std::wstring_view Text(AppLanguage language, UiText text);

} // namespace win32mqtt

#pragma once

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
};

AppLanguage DefaultAppLanguage();
std::wstring_view Text(AppLanguage language, UiText text);

} // namespace win32mqtt

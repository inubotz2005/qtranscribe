pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QTranscribe

ColumnLayout {
    id: root

    signal navigateRequested(string target)
    signal clipboardWarningRequested

    spacing: Theme.spacingMd

    StatusBanner {
        visible: TextInjectorClient.clipboardWarningRequired && !TextInjectorClient.clipboardBannerDismissed
        bannerType: "warning"
        title: qsTr("Clipboard Overwrite Notice")
        message: qsTr(
                     "Transcribing restores copied text, but non-text items (images, files) are overwritten. Paste and save them first, or use a clipboard manager.")
        actionText: qsTr("Learn More")
        onActionClicked: root.clipboardWarningRequested()
        secondaryActionText: qsTr("Dismiss")
        onSecondaryActionClicked: {
            TextInjectorClient.clipboardBannerDismissed = true;
        }
    }

    StatusBanner {
        visible: DictationCoordinator.activeBackend === DictationCoordinator.Groq && !GroqApiClient.apiKeySet
        bannerType: "warning"
        title: qsTr("Groq API Key Required")
        message: qsTr("Configure your free Groq API key in Settings to begin cloud speech transcription.")
        actionText: qsTr("Configure API Key")
        onActionClicked: root.navigateRequested("online")
    }

    StatusBanner {
        visible: DictationCoordinator.activeBackend === DictationCoordinator.Groq && GroqApiClient.apiKeySet
                 && GroqSttClient.errorCategory === GroqSttClient.InvalidApiKey
        bannerType: "warning"
        title: qsTr("Invalid API Key")
        message: GroqSttClient.lastError.length > 0 ? GroqSttClient.lastError : qsTr(
                                                          "Authentication failed. Please verify your Groq API key.")
        actionText: qsTr("Configure API Key")
        onActionClicked: root.navigateRequested("online")
        secondaryActionText: qsTr("Dismiss")
        onSecondaryActionClicked: DictationCoordinator.clearError()
    }

    StatusBanner {
        visible: DictationCoordinator.activeBackend === DictationCoordinator.Groq && GroqSttClient.errorCategory
                 === GroqSttClient.RateLimited && GroqSttClient.retrySecondsRemaining > 0
        bannerType: "warning"
        title: qsTr("Rate Limit Exceeded")
        message: qsTr("Auto-retrying in %1s…").arg(GroqSttClient.retrySecondsRemaining)
        actionText: qsTr("Dismiss")
        onActionClicked: DictationCoordinator.clearError()
    }

    StatusBanner {
        visible: DictationCoordinator.activeBackend === DictationCoordinator.WhisperCpp &&
                 !WhisperSttClient.isModelInstalled
        bannerType: "warning"
        title: qsTr("Offline Whisper Model Missing")
        message: qsTr("Download %1 to start offline transcription.").arg(WhisperSttClient.modelFileName)
        actionText: qsTr("Offline Settings")
        onActionClicked: root.navigateRequested("offline")
    }

    StatusBanner {
        visible: DictationCoordinator.activeBackend === DictationCoordinator.WhisperCpp
                 && WhisperSttClient.isModelInstalled && !WhisperSttClient.isModelLoaded
        bannerType: "info"
        title: qsTr("Loading Whisper Model")
        message: qsTr("Loading offline speech recognition model into memory…")
        actionText: qsTr("Offline Settings")
        onActionClicked: root.navigateRequested("offline")
    }

    StatusBanner {
        visible: !AudioRecorder.hasAudioInputDevice
        bannerType: "warning"
        title: qsTr("No Microphone Detected")
        message: qsTr("Please connect a microphone or check your audio permissions in system settings.")
        actionText: qsTr("Audio Settings")
        onActionClicked: root.navigateRequested("system")
    }

    StatusBanner {
        visible: TextInjectorClient.hasFatalError
        bannerType: "danger"
        title: qsTr("Direct Typing Service Stopped")
        message: TextInjectorClient.fatalErrorMessage.length > 0 ? TextInjectorClient.fatalErrorMessage : qsTr(
                                                                       "The background key injection daemon encountered an error. Text will fallback to clipboard paste until restarted.")
        actionText: qsTr("Restart Service")
        onActionClicked: TextInjectorClient.restartService()
        secondaryActionText: qsTr("Settings")
        onSecondaryActionClicked: root.navigateRequested("system")
    }

    StatusBanner {
        visible: DictationCoordinator.dictationState === DictationCoordinator.Error && (
                     DictationCoordinator.activeBackend !== DictationCoordinator.Groq || (GroqSttClient.errorCategory
                                                                                          !== GroqSttClient.InvalidApiKey
                                                                                          && (GroqSttClient.errorCategory
                                                                                              !== GroqSttClient.RateLimited
                                                                                              || GroqSttClient.retrySecondsRemaining
                                                                                              === 0)))
        bannerType: "danger"
        title: DictationCoordinator.activeBackend === DictationCoordinator.WhisperCpp ? qsTr(
                                                                                            "Offline Transcription Failed") :
                                                                                        qsTr("Transcription Failed")
        message: DictationCoordinator.lastError.length > 0 ? DictationCoordinator.lastError : qsTr(
                                                                 "An error occurred during transcription.")
        actionText: qsTr("Retry Transcription")
        onActionClicked: DictationCoordinator.retryTranscription()
        secondaryActionText: qsTr("Dismiss")
        onSecondaryActionClicked: DictationCoordinator.clearError()
    }
}

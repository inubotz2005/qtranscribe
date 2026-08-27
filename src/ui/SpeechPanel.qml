pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import QTranscribe
import "controls"

Item {
    id: root

    signal navigateRequested(string target)

    implicitWidth: 620
    implicitHeight: 540

    property bool showCopySuccess: false

    Timer {
        id: copyFeedbackTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root.showCopySuccess = false;
        }
    }

    ShortcutGuideDialog {
        id: shortcutGuideDialog
    }

    ClipboardWarningDialog {
        id: clipboardWarningDialog
        onAccepted: {
            DictationCoordinator.startRecording();
        }
    }

    T.ScrollView {
        id: speechScrollView
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: speechScrollView.availableWidth
            spacing: Theme.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                Layout.bottomMargin: Theme.spacingSm
                spacing: 4

                StyledText {
                    text: qsTr("Dictate")
                    variant: "heading"
                }

                StyledText {
                    text: qsTr("Real-time voice dictation and transcription pad")
                    variant: "caption"
                    colorRole: "secondary"
                }
            }

            StatusBanner {
                visible: TextInjectorClient.clipboardWarningRequired && !TextInjectorClient.clipboardBannerDismissed
                bannerType: "warning"
                title: qsTr("Clipboard Overwrite Notice")
                message: qsTr(
                             "Transcribing restores copied text, but non-text items (images, files) are overwritten. Paste and save them first, or use a clipboard manager.")
                actionText: qsTr("Learn More")
                onActionClicked: {
                    clipboardWarningDialog.open();
                }
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
                visible: DictationCoordinator.activeBackend === DictationCoordinator.Groq
                         && GroqSttClient.errorCategory === GroqSttClient.RateLimited
                         && GroqSttClient.retrySecondsRemaining > 0
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
                             DictationCoordinator.activeBackend !== DictationCoordinator.Groq || (
                                 GroqSttClient.errorCategory !== GroqSttClient.InvalidApiKey && (
                                     GroqSttClient.errorCategory !== GroqSttClient.RateLimited
                                     || GroqSttClient.retrySecondsRemaining === 0)))
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

            StyledCard {
                Layout.fillWidth: true
                customRadius: Theme.radiusMd
                customBorderColor: DictationCoordinator.recording ? Theme.colorDanger : Theme.cardBorder
                customBorderWidth: DictationCoordinator.recording ? 2 : 1
                padding: Theme.spacingLg

                Behavior on customBorderColor {
                    ColorAnimation {
                        duration: Theme.animNormal
                    }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 96

                    Item {
                        anchors.centerIn: parent
                        width: 96
                        height: 96

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Theme.colorDanger
                            opacity: DictationCoordinator.recording ? 0.25 : 0.0
                            visible: DictationCoordinator.recording

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: Theme.animNormal
                                }
                            }

                            SequentialAnimation on scale {
                                running: DictationCoordinator.recording
                                loops: Animation.Infinite
                                ScaleAnimator {
                                    to: 1.18
                                    duration: 650
                                }
                                ScaleAnimator {
                                    to: 1.0
                                    duration: 650
                                }
                            }
                        }

                        T.Button {
                            id: recordBtn
                            anchors.fill: parent
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: DictationCoordinator.recording ? qsTr("Stop Dictation") : (
                                                                                  DictationCoordinator.recordingMode
                                                                                  === DictationCoordinator.PushToTalk
                                                                                  ? qsTr("Push to Talk") : qsTr(
                                                                                        "Start Dictation"))
                            Accessible.description: DictationCoordinator.recordingMode
                                                    === DictationCoordinator.PushToTalk ? qsTr(
                                                                                              "Push to talk speech recognition") :
                                                                                          qsTr("Toggle speech recognition recording")
                            enabled: DictationCoordinator.canRecord

                            background: Rectangle {
                                radius: recordBtn.width / 2
                                color: {
                                    if (!recordBtn.enabled)
                                    return Theme.controlBg;
                                    if (DictationCoordinator.recording)
                                    return recordBtn.down ? Theme.buttonDangerBgPressed : (recordBtn.hovered
                                                                                           ? Theme.buttonDangerBgHover :
                                                                                             Theme.buttonDangerBg);
                                    if (DictationCoordinator.isBusy)
                                    return Theme.colorWarning;
                                    return recordBtn.down ? Theme.buttonPrimaryBgPressed : (recordBtn.hovered
                                                                                            ? Theme.buttonPrimaryBgHover :
                                                                                              Theme.buttonPrimaryBg);
                                }
                                border.color: recordBtn.visualFocus ? Theme.focusRingColor : Theme.cardBorder
                                border.width: recordBtn.visualFocus ? Theme.focusRingWidth : 1

                                Behavior on color {
                                    ColorAnimation {
                                        duration: Theme.animFast
                                    }
                                }
                            }

                            contentItem: ColumnLayout {
                                spacing: 2
                                anchors.centerIn: parent

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 28
                                    implicitHeight: 28

                                    Image {
                                        anchors.centerIn: parent
                                        width: 24
                                        height: 24
                                        source: "qrc:/qt/qml/QTranscribe/assets/icons/mic.svg"
                                        sourceSize.width: 24
                                        sourceSize.height: 24
                                        visible: !DictationCoordinator.recording && !DictationCoordinator.isBusy
                                        smooth: true
                                    }

                                    Image {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 22
                                        source: "qrc:/qt/qml/QTranscribe/assets/icons/stop.svg"
                                        sourceSize.width: 22
                                        sourceSize.height: 22
                                        visible: DictationCoordinator.recording
                                        smooth: true
                                    }

                                    Image {
                                        anchors.centerIn: parent
                                        width: 24
                                        height: 24
                                        source: "qrc:/qt/qml/QTranscribe/assets/icons/spinner.svg"
                                        sourceSize.width: 24
                                        sourceSize.height: 24
                                        visible: DictationCoordinator.isBusy
                                        smooth: true

                                        RotationAnimation on rotation {
                                            running: DictationCoordinator.isBusy
                                            loops: Animation.Infinite
                                            from: 0
                                            to: 360
                                            duration: 1000
                                        }
                                    }
                                }

                                StyledText {
                                    text: DictationCoordinator.recording ? qsTr("Stop") : (DictationCoordinator.isBusy
                                                                                           ? qsTr("Processing…") : (
                                                                                                 DictationCoordinator.recordingMode
                                                                                                 === DictationCoordinator.PushToTalk
                                                                                                 ? qsTr("Talk") : qsTr(
                                                                                                       "Record")))
                                    variant: "caption"
                                    customWeight: Font.DemiBold
                                    colorRole: "onAccent"
                                    Layout.alignment: Qt.AlignHCenter
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            onClicked: {
                                if (!DictationCoordinator.recording && TextInjectorClient.clipboardWarningRequired) {
                                    clipboardWarningDialog.open();
                                } else {
                                    DictationCoordinator.toggleRecording();
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    visible: DictationCoordinator.recording

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 5

                        Repeater {
                            model: 7
                            delegate: Item {
                                id: waveformBar
                                required property int index
                                Layout.preferredWidth: 4
                                Layout.preferredHeight: 28

                                readonly property real multiplier: {
                                    switch (index) {
                                        case 0:
                                        case 6:
                                        return 0.5;
                                        case 1:
                                        case 5:
                                        return 0.8;
                                        case 2:
                                        case 4:
                                        return 1.1;
                                        default:
                                        return 1.4;
                                    }
                                }

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 4
                                    height: Math.max(4, 28 * Math.min(DictationCoordinator.audioLevel
                                                                      * waveformBar.multiplier, 1.0))

                                    radius: Theme.radiusXs / 2
                                    color: Theme.colorSuccess

                                    Behavior on height {
                                        NumberAnimation {
                                            duration: 50
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                StyledText {
                    id: audioStatusText
                    text: DictationCoordinator.statusMessage
                    variant: "caption"
                    colorRole: "secondary"
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: audioStatusText.text.length > 0
                }

                StyledText {
                    id: modeHintText
                    text: DictationCoordinator.recordingMode === DictationCoordinator.PushToTalk ? qsTr(
                                                                                                       "Push-to-Talk: Hold Ctrl+Shift+Space to talk") :
                                                                                                   qsTr("Toggle Mode: Press Ctrl+Shift+Space to start / stop")
                    variant: "caption"
                    colorRole: "secondary"
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    visible: !DictationCoordinator.recording && !DictationCoordinator.isBusy
                }

                Item {
                    id: systemStatusContainer
                    visible: DictationCoordinator.systemShortcutHasIssue || DictationCoordinator.directTypingHasIssue
                    Layout.fillWidth: true
                    implicitHeight: 32

                    Rectangle {
                        id: systemStatusPill
                        anchors.centerIn: parent
                        implicitHeight: 32
                        implicitWidth: footerStatusRow.implicitWidth + (Theme.spacingMd * 2)
                        radius: Theme.radiusCircle
                        color: Theme.cardBgSubtle
                        border.color: Theme.cardBorder
                        border.width: 1

                        RowLayout {
                            id: footerStatusRow
                            anchors.centerIn: parent
                            spacing: Theme.spacingMd

                            RowLayout {
                                id: shortcutStatusRow
                                visible: DictationCoordinator.systemShortcutHasIssue
                                spacing: 6

                                StyledIcon {
                                    source: "qrc:/qt/qml/QTranscribe/assets/icons/keyboard.svg"
                                    size: 14
                                    color: Theme.textSecondary
                                    Layout.alignment: Qt.AlignVCenter
                                    opacity: 0.8
                                }

                                Rectangle {
                                    implicitWidth: 6
                                    implicitHeight: 6
                                    radius: 3
                                    Layout.alignment: Qt.AlignVCenter
                                    color: DictationCoordinator.systemShortcutSupported ? Theme.colorWarning :
                                                                                          Theme.colorDanger
                                }

                                StyledText {
                                    text: DictationCoordinator.systemShortcutStatus.length > 0
                                          ? DictationCoordinator.systemShortcutStatus : qsTr(
                                                "Global shortcut unavailable")
                                    variant: "caption"
                                    colorRole: "secondary"
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                StyledButton {
                                    id: setupGuideBtn
                                    text: qsTr("Setup Guide")
                                    visible: !DictationCoordinator.systemShortcutSupported
                                    variant: "flat"
                                    size: "small"
                                    onClicked: shortcutGuideDialog.open()
                                }
                            }

                            StyledDivider {
                                id: statusDivider
                                visible: DictationCoordinator.systemShortcutHasIssue
                                         && DictationCoordinator.directTypingHasIssue
                                orientation: Qt.Vertical
                                implicitHeight: 14
                                Layout.alignment: Qt.AlignVCenter
                            }

                            RowLayout {
                                id: directTypingStatusRow
                                visible: DictationCoordinator.directTypingHasIssue
                                spacing: 6

                                StyledIcon {
                                    source: "qrc:/qt/qml/QTranscribe/assets/icons/bolt.svg"
                                    size: 13
                                    color: Theme.textSecondary
                                    Layout.alignment: Qt.AlignVCenter
                                    opacity: 0.8
                                }

                                Rectangle {
                                    implicitWidth: 6
                                    implicitHeight: 6
                                    radius: 3
                                    Layout.alignment: Qt.AlignVCenter
                                    color: DictationCoordinator.directTypingFatalError ? Theme.colorDanger :
                                                                                         Theme.colorWarning
                                }

                                StyledText {
                                    text: DictationCoordinator.directTypingStatus
                                    variant: "caption"
                                    colorRole: "secondary"
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }

            StyledCard {
                Layout.fillWidth: true
                customRadius: Theme.radiusMd
                padding: Theme.spacingMd

                RowLayout {
                    Layout.fillWidth: true

                    StyledText {
                        text: qsTr("Dictation Pad")
                        variant: "body"
                        customWeight: Font.DemiBold
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    StyledText {
                        text: qsTr("%1 words • %2 characters").arg(DictationCoordinator.dictationWordCount).arg(
                                  DictationCoordinator.dictationCharCount)
                        variant: "caption"
                        colorRole: "secondary"
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    implicitHeight: 96
                    clip: true

                    T.TextArea {
                        id: testingGroundText
                        text: DictationCoordinator.dictationPadText
                        placeholderText: qsTr("Text will appear here when dictating…")
                        placeholderTextColor: Theme.textPlaceholder
                        font.pixelSize: Theme.fontSizeBody
                        wrapMode: Text.WordWrap
                        selectByMouse: true
                        color: Theme.textPrimary
                        leftPadding: Theme.spacingSm + 2
                        rightPadding: Theme.spacingSm + 2
                        topPadding: Theme.spacingSm
                        bottomPadding: Theme.spacingSm

                        background: Rectangle {
                            color: Theme.inputBg
                            border.color: testingGroundText.activeFocus ? Theme.focusRingColor : Theme.inputBorder
                            border.width: testingGroundText.activeFocus ? Theme.focusRingWidth : 1
                            radius: Theme.radiusSm
                        }

                        onTextChanged: {
                            if (activeFocus) {
                                DictationCoordinator.dictationPadText = text;
                            } else {
                                cursorPosition = text.length;
                            }
                        }
                    }
                }

                Connections {
                    target: DictationCoordinator
                    function onDictationPadTextChanged(): void {
                        testingGroundText.cursorPosition = testingGroundText.text.length;
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    StyledButton {
                        id: copyPadBtn
                        text: root.showCopySuccess ? qsTr("Copied!") : qsTr("Copy")
                        iconSource: root.showCopySuccess ? "qrc:/qt/qml/QTranscribe/assets/icons/check.svg" :
                                                           "qrc:/qt/qml/QTranscribe/assets/icons/copy.svg"
                        enabled: DictationCoordinator.dictationCharCount > 0
                        size: "small"
                        variant: root.showCopySuccess ? "primary" : "secondary"
                        onClicked: {
                            DictationCoordinator.copyDictationPad();
                            root.showCopySuccess = true;
                            copyFeedbackTimer.restart();
                        }
                    }

                    StyledButton {
                        id: clearPadBtn
                        text: qsTr("Clear")
                        iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/trash.svg"
                        enabled: DictationCoordinator.dictationCharCount > 0
                        size: "small"
                        onClicked: {
                            DictationCoordinator.clearDictationPad();
                        }
                    }
                }
            }
        }
    }
}

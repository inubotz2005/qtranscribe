pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import QTranscribe
import "controls"
import "settings"

ApplicationWindow {
    id: root
    width: 960
    height: 660
    minimumWidth: 760
    minimumHeight: 520
    visible: false
    title: qsTr("QTranscribe")
    color: Theme.windowBg

    property string currentSectionId: "dictate"

    readonly property var sectionToViewIndex: ({
                                                   "dictate": 0,
                                                   "history": 1,
                                                   "cloudUsage": 2,
                                                   "online": 3,
                                                   "offline": 4,
                                                   "system": 5,
                                                   "about": 6,
                                                   "license": 7
                                               })

    function normalizeSectionId(target: string): string {
        switch (target) {
        case "apiKey":
        case "enhancement":
            return "online";
        case "dictation":
            return SpeechController.activeBackend === SpeechController.TranscriptionBackend.Groq ? "online" : "offline";
        case "activity":
            return "cloudUsage";
        default:
            return target;
        }
    }

    function navigateToSection(target: var) {
        if (typeof target === "number") {
            if (target >= 0 && target < navModel.count) {
                root.currentSectionId = navModel.get(target).sectionId;
            }
            return;
        }
        if (typeof target === "string") {
            const normalized = normalizeSectionId(target);
            if (normalized in sectionToViewIndex) {
                root.currentSectionId = normalized;
            }
        }
    }

    function rebuildNavModel() {
        const isOnline = SpeechController.activeBackend === SpeechController.TranscriptionBackend.Groq;
        navModel.clear();

        navModel.append({
                            section: "MAIN",
                            title: qsTr("Dictate"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/mic.svg",
                            sectionId: "dictate"
                        });
        navModel.append({
                            section: "MAIN",
                            title: qsTr("History"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/history.svg",
                            sectionId: "history"
                        });
        if (isOnline) {
            navModel.append({
                                section: "MAIN",
                                title: qsTr("Cloud Usage"),
                                iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/activity.svg",
                                sectionId: "cloudUsage"
                            });
        }

        navModel.append({
                            section: "PREFERENCES",
                            title: qsTr("Dictation"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/speech.svg",
                            sectionId: isOnline ? "online" : "offline"
                        });
        navModel.append({
                            section: "PREFERENCES",
                            title: qsTr("System & Audio"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/keyboard.svg",
                            sectionId: "system"
                        });

        navModel.append({
                            section: "INFO",
                            title: qsTr("About"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/info.svg",
                            sectionId: "about"
                        });
        navModel.append({
                            section: "INFO",
                            title: qsTr("License"),
                            iconSource: "qrc:/qt/qml/QTranscribe/assets/icons/license.svg",
                            sectionId: "license"
                        });
    }

    Component.onCompleted: rebuildNavModel()

    property bool isQuitting: false

    function quitApplication() {
        root.isQuitting = true;
        Qt.quit();
    }

    onClosing: close => {
        if (!root.isQuitting) {
            close.accepted = false;
            root.hide();
        }
    }

    Platform.SystemTrayIcon {
        id: trayIcon
        visible: true
        icon.name: SpeechController.recording ? TrayIconHelper.trayIconRecordingName : TrayIconHelper.trayIconName
        icon.source: SpeechController.recording ? TrayIconHelper.trayIconRecordingPath(Theme.isDark) :
                                                  TrayIconHelper.trayIconPath(Theme.isDark)
        tooltip: qsTr("QTranscribe")

        menu: Platform.Menu {
            Platform.MenuItem {
                text: qsTr("Open QTranscribe")
                onTriggered: {
                    root.show();
                    root.raise();
                    root.requestActivate();
                }
            }
            Platform.MenuItem {
                text: qsTr("Quit")
                onTriggered: root.quitApplication()
            }
        }

        onActivated: activationReason => {
            if (activationReason === Platform.SystemTrayIcon.Trigger || activationReason
                    === Platform.SystemTrayIcon.DoubleClick) {
                root.show();
                root.raise();
                root.requestActivate();
            }
        }
    }

    Connections {
        target: SpeechController
        function onRequestShowWindow() {
            root.show();
            root.raise();
            root.requestActivate();
        }
        function onRequestQuitApp() {
            root.quitApplication();
        }
        function onActiveBackendChanged() {
            root.rebuildNavModel();
            if (SpeechController.activeBackend === SpeechController.TranscriptionBackend.Groq) {
                if (root.currentSectionId === "offline") {
                    root.currentSectionId = "online";
                }
            } else {
                if (root.currentSectionId === "online" || root.currentSectionId === "cloudUsage") {
                    root.currentSectionId = "offline";
                }
            }
        }
    }

    ListModel {
        id: navModel
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.width < 840 ? 180 : Theme.sidebarWidth
            Layout.fillHeight: true
            color: Theme.sidebarBg

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: Theme.animFast
                    easing.type: Easing.OutCubic
                }
            }

            StyledDivider {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: Qt.Vertical
                dividerColor: Theme.sidebarBorder
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingSm

                ListView {
                    id: navListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spacingXs
                    clip: true
                    spacing: 2
                    model: navModel

                    delegate: Item {
                        id: navDelegate
                        required property string section
                        required property string title
                        required property string iconSource
                        required property string sectionId
                        required property int index

                        readonly property bool isSelected: root.currentSectionId === navDelegate.sectionId
                        readonly property bool isFirstInSection: {
                            if (navDelegate.index <= 0)
                            return true;
                            const prev = navModel.get(navDelegate.index - 1);
                            return !prev || prev.section !== navDelegate.section;
                        }

                        width: navListView.width
                        implicitHeight: (navDelegate.isFirstInSection ? 32 : 0) + 34

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            StyledText {
                                text: navDelegate.section
                                variant: "small"
                                customWeight: Font.Bold
                                colorRole: "tertiary"
                                visible: navDelegate.isFirstInSection
                                Layout.leftMargin: Theme.spacingSm
                                Layout.topMargin: navDelegate.index === 0 ? 4 : Theme.spacingMd
                                Layout.bottomMargin: 4
                            }

                            Rectangle {
                                id: itemPill
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: Theme.radiusSm
                                color: {
                                    if (navDelegate.isSelected)
                                    return Theme.sidebarItemSelected;
                                    if (navMouse.containsMouse)
                                    return Theme.sidebarItemHover;
                                    return "transparent";
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingSm + 2
                                    anchors.rightMargin: Theme.spacingSm
                                    spacing: Theme.spacingSm

                                    StyledIcon {
                                        source: navDelegate.iconSource
                                        size: 16
                                        color: navDelegate.isSelected ? Theme.accentColor : (navMouse.containsMouse
                                                                                             ? Theme.textPrimary :
                                                                                               Theme.textSecondary)
                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    StyledText {
                                        text: navDelegate.title
                                        variant: "body"
                                        customWeight: navDelegate.isSelected ? Font.DemiBold : Font.Normal
                                        customColor: navDelegate.isSelected ? Theme.accentColor : (
                                                                                  navMouse.containsMouse
                                                                                  ? Theme.textPrimary :
                                                                                    Theme.textSecondary)
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: navMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.navigateToSection(navDelegate.sectionId);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                id: viewStack
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                currentIndex: root.sectionToViewIndex[root.currentSectionId] !== undefined
                ? root.sectionToViewIndex[root.currentSectionId] : 0

                onCurrentIndexChanged: {
                    viewTransitionAnim.restart();
                }

                SpeechPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onNavigateRequested: target => {
                        root.navigateToSection(target);
                    }
                }

                HistoryView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                UsageView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onNavigateRequested: target => {
                        root.navigateToSection(target);
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    OnlineSettingsPage {
                        width: parent.width
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    OfflineSettingsPage {
                        width: parent.width
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    SystemSettingsPage {
                        width: parent.width
                    }
                }

                CreditsView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                LicenseView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            OpacityAnimator {
                id: viewTransitionAnim
                target: viewStack
                from: 0.4
                to: 1.0
                duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
        }
    }
}

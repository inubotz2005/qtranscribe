pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QTranscribe
import "controls"

Item {
    id: root

    signal shortcutGuideRequested

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
                    color: DictationCoordinator.systemShortcutSupported ? Theme.colorWarning : Theme.colorDanger
                }

                StyledText {
                    text: DictationCoordinator.systemShortcutStatus.length > 0
                          ? DictationCoordinator.systemShortcutStatus : qsTr("Global shortcut unavailable")
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
                    onClicked: root.shortcutGuideRequested()
                }
            }

            StyledDivider {
                id: statusDivider
                visible: DictationCoordinator.systemShortcutHasIssue && DictationCoordinator.directTypingHasIssue
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
                    color: DictationCoordinator.directTypingFatalError ? Theme.colorDanger : Theme.colorWarning
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

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: window
    width: 1180
    height: 660
    minimumWidth: 900
    minimumHeight: 540
    visible: true
    title: "NiceTOTP Configurator Next"
    color: palette.canvas

    QtObject {
        id: palette
        readonly property var themes: ({
            "gagan-green": { canvas: "#101614", sidebar: "#141c19", panel: "#18211e", raised: "#202c27", border: "#2b3933", text: "#e8eee9", muted: "#91a199", accent: "#9ad96b", accentDeep: "#304a2a", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" },
            "classic-gray": { canvas: "#242629", sidebar: "#202225", panel: "#2e3135", raised: "#393d42", border: "#4a4f55", text: "#eff1f3", muted: "#a7adb4", accent: "#c3c9ce", accentDeep: "#44494f", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" },
            "pollution-gray": { canvas: "#090b0c", sidebar: "#0d0f11", panel: "#141719", raised: "#1c2023", border: "#2c3135", text: "#f0f2f3", muted: "#8f989e", accent: "#b9c1c5", accentDeep: "#262d31", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" },
            "island-blue": { canvas: "#0d1a22", sidebar: "#10212a", panel: "#142731", raised: "#1c3a48", border: "#2d5668", text: "#e7f5fa", muted: "#9ab8c5", accent: "#67cde0", accentDeep: "#1c4656", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" },
            "blood-red": { canvas: "#181011", sidebar: "#1e1315", panel: "#281a1d", raised: "#382326", border: "#59363b", text: "#f4e8e9", muted: "#b99a9d", accent: "#ef777d", accentDeep: "#54292e", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" },
            "problem-purple": { canvas: "#15111a", sidebar: "#1a1421", panel: "#211a29", raised: "#30263d", border: "#514062", text: "#f1ebf7", muted: "#ad9db9", accent: "#c494f4", accentDeep: "#413151", danger: "#873b36", dangerHover: "#a94942", dangerText: "#ffb0a9" }
        })
        readonly property var active: themes[device.appTheme] || themes["gagan-green"]
        readonly property color canvas: active.canvas
        readonly property color sidebar: active.sidebar
        readonly property color panel: active.panel
        readonly property color panelRaised: active.raised
        readonly property color border: active.border
        readonly property color text: active.text
        readonly property color muted: active.muted
        readonly property color green: active.accent
        readonly property color greenDeep: active.accentDeep
        readonly property color red: "#e6655b"
        readonly property color amber: "#e4b45f"
        readonly property color danger: active.danger
        readonly property color dangerHover: active.dangerHover
        readonly property color dangerText: active.dangerText
    }

    property color controlFill: palette.panelRaised
    property color controlHover: palette.panelRaised
    property color controlForeground: palette.text
    property color controlBorder: palette.border
    property color controlAccent: palette.green
    property color lockInterior: palette.canvas

    property string page: "accounts"
    property int pendingDeleteId: -1
    property string uf2Path: ""
    property string nrfutilPackagePath: ""
    property var themeIds: ["gagan-green", "classic-gray", "pollution-gray", "island-blue", "blood-red", "problem-purple"]
    property color fieldColor: palette.panelRaised
    property color fieldText: palette.text
    property color fieldMuted: palette.muted
    property color fieldBorder: palette.border
    property color fieldAccent: palette.green

    function syncAccountModel() {
        while (accountModel.count > device.accounts.length)
            accountModel.remove(accountModel.count - 1)
        for (var index = 0; index < device.accounts.length; ++index) {
            var account = {
                "accountId": device.accounts[index].id,
                "username": device.accounts[index].username,
                "code": device.accounts[index].code,
                "remaining": device.accounts[index].remaining
            }
            if (index < accountModel.count)
                accountModel.set(index, account)
            else
                accountModel.append(account)
        }
    }

    Component.onCompleted: syncAccountModel()

    ListModel { id: accountModel; objectName: "accountsModel" }

    Connections {
        target: device
        function onAccountsChanged() { window.syncAccountModel() }
    }

    component FlatButton: Button {
        id: control
        property color fillColor: window.controlFill
        property color hoverColor: window.controlHover
        property color foreground: window.controlForeground
        implicitHeight: 42
        padding: 14
        contentItem: Text {
            text: control.text
            color: control.foreground
            font.pixelSize: 14
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: control.down ? control.hoverColor : control.hovered ? control.hoverColor : control.fillColor
            border.color: control.hovered ? window.controlAccent : window.controlBorder
            border.width: 1
        }
    }

    component LockMark: Item {
        property color color: "#e6655b"
        implicitWidth: 30
        implicitHeight: 34
        Rectangle {
            x: 7
            y: 1
            width: 16
            height: 20
            radius: 8
            color: "transparent"
            border.width: 3
            border.color: parent.color
        }
        Rectangle {
            x: 3
            y: 15
            width: 24
            height: 18
            radius: 4
            color: parent.color
        }
        Rectangle {
            x: 13
            y: 20
            width: 4
            height: 7
            radius: 2
            color: window.lockInterior
        }
    }

    component ThemedTextField: TextField {
        id: textField
        color: window.fieldText
        placeholderTextColor: window.fieldMuted
        selectByMouse: true
        background: Rectangle {
            radius: 8
            color: window.fieldColor
            border.width: textField.activeFocus ? 2 : 1
            border.color: textField.activeFocus ? window.fieldAccent : window.fieldBorder
        }
    }

    Rectangle {
        id: sidebar
        width: 224
        color: palette.sidebar
        border.color: palette.border
        border.width: 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 28

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    id: brandTile
                    width: 34
                    height: 34
                    radius: 9
                    color: palette.greenDeep
                    clip: true
                    Image {
                        objectName: "brandIcon"
                        anchors.fill: parent
                        source: "icon.webp"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }
                Column {
                    spacing: 2
                    Text { text: "NiceTOTP"; color: palette.text; font.pixelSize: 17; font.weight: Font.Bold }
                    Text { text: "CONFIGURATOR NEXT"; color: palette.muted; font.pixelSize: 9 }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "DEVICE"
                    color: palette.muted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    leftPadding: 10
                    bottomPadding: 5
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: "◫   Accounts"
                    fillColor: window.page === "accounts" ? palette.greenDeep : "transparent"
                    hoverColor: palette.panelRaised
                    foreground: window.page === "accounts" ? palette.green : palette.text
                    onClicked: window.page = "accounts"
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: "⇧   Firmware"
                    fillColor: window.page === "firmware" ? palette.greenDeep : "transparent"
                    hoverColor: palette.panelRaised
                    foreground: window.page === "firmware" ? palette.green : palette.text
                    onClicked: window.page = "firmware"
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: "⚙   Device settings"
                    fillColor: window.page === "device-settings" ? palette.greenDeep : "transparent"
                    hoverColor: palette.panelRaised
                    foreground: window.page === "device-settings" ? palette.green : palette.text
                    onClicked: window.page = "device-settings"
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: "◐   App settings"
                    fillColor: window.page === "app-settings" ? palette.greenDeep : "transparent"
                    hoverColor: palette.panelRaised
                    foreground: window.page === "app-settings" ? palette.green : palette.text
                    onClicked: window.page = "app-settings"
                }
            }

            Item { Layout.fillHeight: true }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 88
                radius: 7
                color: palette.panel
                border.color: palette.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 13
                    spacing: 12
                    Rectangle {
                        width: 9
                        height: 9
                        radius: 5
                        color: device.unlocked ? palette.green : palette.red
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: device.unlocked ? "Device unlocked" : "Device locked"
                            color: palette.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: device.connected ? "NiceTOTP detected" : "Waiting for device"
                            color: palette.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 72
            color: palette.canvas
            border.color: palette.border
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 30
                anchors.rightMargin: 30
                spacing: 12
                ColumnLayout {
                    spacing: 3
                    Text {
                        text: window.page === "accounts" ? "Accounts" : window.page === "firmware" ? "Firmware updates" : window.page === "device-settings" ? "Device settings" : "App settings"
                        color: palette.text
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: device.status
                        color: device.unlocked ? palette.green : palette.muted
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillWidth: true }
                Item {
                    visible: !device.unlocked
                    Layout.preferredWidth: 17
                    Layout.preferredHeight: 17
                    LockMark {
                        anchors.centerIn: parent
                        transform: Scale { xScale: 0.5; yScale: 0.5; origin.x: width / 2; origin.y: height / 2 }
                    }
                }
                Rectangle {
                    width: 9
                    height: 9
                    radius: 5
                    color: device.unlocked ? palette.green : palette.red
                }
                Text {
                    text: device.unlocked ? "UNLOCKED" : device.connected ? "LOCKED" : "NOT CONNECTED"
                    color: device.unlocked ? palette.green : palette.muted
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: palette.canvas

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 30
                spacing: 20
                visible: window.page === "accounts"

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        spacing: 4
                        Text { text: "Your vault"; color: palette.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Text {
                            text: device.unlocked ? device.accounts.length + " saved accounts" : "Account labels appear after you unlock the device"
                            color: palette.muted
                            font.pixelSize: 12
                        }
                    }
                    Item { Layout.fillWidth: true }
                    FlatButton {
                        text: "Import QR"
                        enabled: device.unlocked
                        onClicked: qrSourceDialog.open()
                    }
                    FlatButton {
                        text: "+   Add account"
                        enabled: device.unlocked
                        fillColor: palette.green
                        hoverColor: palette.green
                        foreground: "#102014"
                        onClicked: addDialog.open()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: palette.panel
                    border.color: palette.border
                    visible: device.unlocked

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "ACCOUNT"; color: palette.muted; font.pixelSize: 10; font.weight: Font.Bold }
                            Item { Layout.fillWidth: true }
                            Text { text: "CURRENT CODE"; color: palette.muted; font.pixelSize: 10; font.weight: Font.Bold; rightPadding: 8 }
                            Text { text: "VALID FOR"; color: palette.muted; font.pixelSize: 10; font.weight: Font.Bold; rightPadding: 52 }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: palette.border }
                        ListView {
                            id: accountList
                            objectName: "accountList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 5
                            model: accountModel
                            ScrollBar.vertical: ScrollBar { }
                            delegate: Rectangle {
                                required property int accountId
                                required property string username
                                required property string code
                                required property int remaining
                                width: accountList.width
                                height: 54
                                radius: 5
                                color: rowHover.containsMouse ? palette.panelRaised : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Rectangle {
                                        width: 30
                                        height: 30
                                        radius: 7
                                        color: palette.greenDeep
                                        Text {
                                            anchors.centerIn: parent
                                            text: (username || "?").charAt(0).toUpperCase()
                                            color: palette.green
                                            font.pixelSize: 14
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: username
                                        color: palette.text
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.preferredWidth: 92
                                        text: code || "------"
                                        color: code ? palette.green : palette.muted
                                        font.family: "monospace"
                                        font.pixelSize: 17
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        text: remaining > 0 ? String(remaining).padStart(2, "0") + "s" : "--"
                                        color: palette.muted
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    ToolButton {
                                        id: deleteButton
                                        text: "×"
                                        width: 34
                                        height: 34
                                        onClicked: {
                                            window.pendingDeleteId = accountId
                                            deleteDialog.open()
                                        }
                                        contentItem: Text {
                                            text: "×"
                                            color: deleteButton.hovered ? palette.red : palette.muted
                                            font.pixelSize: 21
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle { color: "transparent" }
                                    }
                                }
                                MouseArea {
                                    id: rowHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                    z: -1
                                }
                                TapHandler {
                                    onTapped: device.copyCode(accountId)
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: device.unlocked && accountModel.count === 0
                                text: "Your vault is empty"
                                color: palette.muted
                                font.pixelSize: 14
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: palette.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            FlatButton {
                                text: "↻"
                                width: 42
                                padding: 0
                                ToolTip.visible: hovered
                                ToolTip.text: "Refresh accounts"
                                onClicked: device.refreshAccounts()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: palette.panel
                    border.color: palette.border
                    visible: !device.unlocked
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 44, 430)
                        spacing: 15
                        Text {
                            Layout.fillWidth: true
                            text: device.connected ? "Unlock on your NiceTOTP" : "Waiting for NiceTOTP"
                            color: palette.text
                            font.pixelSize: 21
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            Layout.fillWidth: true
                            text: device.connected
                                ? "Use the device buttons to enter your passcode. Your account list will load automatically."
                                : "Connect the device over USB. It will be detected automatically when its serial interface is available."
                            color: palette.muted
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !device.connected
                            text: "If it is already connected, unlock it on the device to expose its secure configurator interface."
                            color: palette.muted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

            }

            ScrollView {
                anchors.fill: parent
                anchors.margins: 30
                visible: window.page === "device-settings"
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 18

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 82
                        radius: 7
                        color: palette.panel
                        border.color: palette.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            LockMark { color: device.unlocked ? palette.green : palette.red }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text { text: "Device access"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Text { text: device.unlocked ? "The encrypted vault is open for this session." : "Unlock the device physically to change settings."; color: palette.muted; font.pixelSize: 12 }
                            }
                            FlatButton {
                                text: "Lock device"
                                enabled: device.unlocked
                                onClicked: device.lockDevice()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: calibrationColumn.implicitHeight + 36
                        radius: 7
                        color: palette.panel
                        border.color: palette.border
                        ColumnLayout {
                            id: calibrationColumn
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Clock and calibration"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                FlatButton { text: "Read status"; enabled: device.unlocked; onClicked: device.getCalibration() }
                            }
                            Text { Layout.fillWidth: true; text: device.calibration; color: palette.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            RowLayout {
                                Layout.fillWidth: true
                                FlatButton { text: "Set device time"; enabled: device.unlocked; onClicked: device.setDeviceTime() }
                                Item { Layout.fillWidth: true }
                                Text { text: "Aging offset"; color: palette.muted; font.pixelSize: 12 }
                                SpinBox {
                                    id: calibrationInput
                                    from: -128
                                    to: 127
                                    value: device.calibrationOffset
                                    editable: true
                                    enabled: device.unlocked
                                    Layout.preferredWidth: 130
                                    contentItem: TextInput {
                                        text: calibrationInput.displayText
                                        color: palette.text
                                        selectionColor: palette.greenDeep
                                        selectedTextColor: palette.text
                                        font.pixelSize: 14
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        readOnly: !calibrationInput.editable
                                        validator: calibrationInput.validator
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: palette.panelRaised
                                        border.width: calibrationInput.activeFocus ? 2 : 1
                                        border.color: calibrationInput.activeFocus ? palette.green : palette.border
                                    }
                                    up.indicator: Rectangle {
                                        x: calibrationInput.width - width
                                        width: 27
                                        height: calibrationInput.height / 2
                                        color: "transparent"
                                        Text { anchors.centerIn: parent; text: "▲"; color: palette.green; font.pixelSize: 8 }
                                    }
                                    down.indicator: Rectangle {
                                        x: calibrationInput.width - width
                                        y: calibrationInput.height / 2
                                        width: 27
                                        height: calibrationInput.height / 2
                                        color: "transparent"
                                        Text { anchors.centerIn: parent; text: "▼"; color: palette.green; font.pixelSize: 8 }
                                    }
                                }
                                FlatButton {
                                    text: "Apply"
                                    enabled: device.unlocked
                                    onClicked: device.setManualCalibration(String(calibrationInput.value))
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                FlatButton { text: "Allow auto calibration"; enabled: device.unlocked; onClicked: device.setCalibrationLocked(false) }
                                FlatButton { text: "Lock auto calibration"; enabled: device.unlocked; onClicked: device.setCalibrationLocked(true) }
                                FlatButton { text: "Clear calibration"; enabled: device.unlocked; onClicked: clearCalibrationDialog.open() }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: passcodeColumn.implicitHeight + 36
                        radius: 7
                        color: palette.panel
                        border.color: palette.border
                        ColumnLayout {
                            id: passcodeColumn
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 8
                            Text { text: "Passcode"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: "A passcode change is confirmed on the physical device. The configurator never receives your passcode."; color: palette.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            FlatButton { text: "Change passcode on device"; enabled: device.unlocked; onClicked: device.setupPasscode() }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: resetColumn.implicitHeight + 36
                        radius: 7
                        color: palette.panel
                        border.color: palette.danger
                        ColumnLayout {
                            id: resetColumn
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10
                            Text { text: "Danger zone"; color: palette.dangerText; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: "Factory reset erases the encrypted vault and reboots the device."; color: palette.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            FlatButton {
                                text: "Clear all accounts…"
                                enabled: device.unlocked
                                fillColor: palette.danger
                                hoverColor: palette.dangerHover
                                onClicked: clearAccountsDialog.open()
                            }
                            FlatButton {
                                text: "Factory reset…"
                                enabled: device.unlocked
                                fillColor: palette.danger
                                hoverColor: palette.dangerHover
                                onClicked: factoryResetDialog.open()
                            }
                        }
                    }

                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 30
                spacing: 18
                visible: window.page === "app-settings"

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 174
                    radius: 7
                    color: palette.panel
                    border.color: palette.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10
                        Text { text: "Appearance"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { text: "Choose an app color theme."; color: palette.muted; font.pixelSize: 12 }
                        ComboBox {
                            id: themePicker
                            implicitWidth: 340
                            implicitHeight: 44
                            model: ["Gagan green", "Classic gray", "Pollution gray", "Blup blup island blue", "Blood red", "Problem purple"]
                            currentIndex: Math.max(0, window.themeIds.indexOf(device.appTheme))
                            onActivated: function(index) { device.setAppTheme(window.themeIds[index]) }
                            contentItem: Text {
                                leftPadding: 13
                                rightPadding: 34
                                text: themePicker.displayText
                                color: palette.text
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            indicator: Text {
                                x: themePicker.width - width - 13
                                y: (themePicker.height - height) / 2
                                text: "⌄"
                                color: palette.muted
                                font.pixelSize: 18
                            }
                            background: Rectangle {
                                radius: 9
                                color: palette.panelRaised
                                border.width: themePicker.activeFocus ? 2 : 1
                                border.color: themePicker.activeFocus ? palette.green : palette.border
                            }
                            delegate: ItemDelegate {
                                id: themeChoice
                                required property int index
                                required property string modelData
                                width: themePicker.width
                                text: themePicker.textAt(index)
                                highlighted: themePicker.highlightedIndex === index
                                contentItem: Text {
                                    leftPadding: 10
                                    text: themeChoice.modelData
                                    color: palette.text
                                    font.pixelSize: 13
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 6
                                    color: themeChoice.highlighted ? window.controlHover : "transparent"
                                }
                            }
                            popup: Popup {
                                y: themePicker.height + 5
                                width: themePicker.width
                                implicitHeight: Math.min(themeOptions.contentHeight + 8, 6 * 42 + 8)
                                padding: 4
                                contentItem: ListView {
                                    id: themeOptions
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: themePicker.popup.visible ? themePicker.delegateModel : null
                                    currentIndex: themePicker.highlightedIndex
                                    delegate: themePicker.delegate
                                    ScrollBar.vertical: ScrollBar { }
                                }
                                background: Rectangle {
                                    radius: 10
                                    color: palette.panel
                                    border.color: palette.border
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    textFormat: Text.RichText
                    text: "NiceTOTP Configurator Next by ICantMakeThings  |  <a href=\"https://github.com/ICantMakeThings/NiceTOTP\">GitHub</a>"
                    color: palette.muted
                    linkColor: palette.green
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 30
                spacing: 16
                visible: window.page === "firmware"

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Firmware updates"; color: palette.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Rectangle {
                        radius: 4
                        color: "#49381d"
                        implicitWidth: wipLabel.implicitWidth + 14
                        implicitHeight: 24
                        Text { id: wipLabel; anchors.centerIn: parent; text: "WORK IN PROGRESS"; color: "#f0c36d"; font.pixelSize: 9; font.weight: Font.Bold }
                    }
                    Item { Layout.fillWidth: true }
                }
                TabBar {
                    id: firmwareMode
                    Layout.fillWidth: true
                    background: Rectangle { radius: 8; color: palette.panelRaised; border.color: palette.border }
                    TabButton {
                        id: uf2ModeTab
                        objectName: "uf2ModeTab"
                        text: "UF2"
                        implicitHeight: 42
                        contentItem: Text {
                            text: uf2ModeTab.text
                            color: uf2ModeTab.checked ? palette.green : palette.muted
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 7
                            color: uf2ModeTab.checked ? palette.greenDeep : "transparent"
                            border.color: uf2ModeTab.checked ? palette.green : "transparent"
                        }
                    }
                    TabButton {
                        id: nrfutilModeTab
                        objectName: "nrfutilModeTab"
                        text: "nrfutil serial DFU"
                        implicitHeight: 42
                        contentItem: Text {
                            text: nrfutilModeTab.text
                            color: nrfutilModeTab.checked ? palette.green : palette.muted
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 7
                            color: nrfutilModeTab.checked ? palette.greenDeep : "transparent"
                            border.color: nrfutilModeTab.checked ? palette.green : "transparent"
                        }
                    }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: firmwareMode.currentIndex

                    Rectangle {
                        radius: 7
                        color: palette.panel
                        border.color: palette.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12
                            Text { text: "UF2 firmware"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                text: "Available .uf2 uploads are detected from every GitHub release. The updater waits for the device's UF2 drive before copying the selected asset."
                                color: palette.muted
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox {
                                    id: releasePicker
                                    Layout.fillWidth: true
                                    implicitHeight: 44
                                    model: device.releaseAssets
                                    textRole: "name"
                                    currentIndex: device.releaseAssets.length > 0 ? 0 : -1
                                    contentItem: Text {
                                        leftPadding: 13
                                        rightPadding: 34
                                        text: releasePicker.currentIndex < 0
                                            ? (device.releaseListLoading ? "Loading GitHub releases…" : "No UF2 releases found")
                                            : releasePicker.displayText
                                        color: palette.text
                                        font.pixelSize: 12
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    indicator: Text {
                                        x: releasePicker.width - width - 13
                                        y: (releasePicker.height - height) / 2
                                        text: "⌄"
                                        color: palette.muted
                                        font.pixelSize: 18
                                    }
                                    background: Rectangle {
                                        radius: 9
                                        color: palette.panelRaised
                                        border.width: releasePicker.activeFocus ? 2 : 1
                                        border.color: releasePicker.activeFocus ? palette.green : palette.border
                                    }
                                    delegate: ItemDelegate {
                                        id: releaseChoice
                                        required property string name
                                        required property int index
                                        width: releasePicker.width
                                        text: name
                                        highlighted: releasePicker.highlightedIndex === index
                                        contentItem: Text {
                                            leftPadding: 10
                                            text: releaseChoice.name
                                            color: palette.text
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            radius: 6
                                            color: releaseChoice.highlighted ? window.controlHover : "transparent"
                                        }
                                    }
                                    popup: Popup {
                                        y: releasePicker.height + 5
                                        width: releasePicker.width
                                        implicitHeight: Math.min(releaseOptions.contentHeight + 8, 8 * 42 + 8)
                                        padding: 4
                                        contentItem: ListView {
                                            id: releaseOptions
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: releasePicker.popup.visible ? releasePicker.delegateModel : null
                                            currentIndex: releasePicker.highlightedIndex
                                            delegate: releasePicker.delegate
                                            ScrollBar.vertical: ScrollBar { }
                                        }
                                        background: Rectangle { radius: 10; color: palette.panel; border.color: palette.border }
                                    }
                                }
                                FlatButton {
                                    text: "↻"
                                    width: 44
                                    padding: 0
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Refresh GitHub releases"
                                    onClicked: device.loadFirmwareReleases()
                                }
                            }
                            FlatButton {
                                text: device.updateBusy ? "Updating…" : "Download and install UF2"
                                enabled: !device.updateBusy && releasePicker.currentIndex >= 0 && device.unlocked
                                fillColor: palette.green
                                hoverColor: palette.greenDeep
                                foreground: palette.canvas
                                onClicked: device.downloadFirmwareAsset(releasePicker.currentIndex)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ThemedTextField { Layout.fillWidth: true; text: window.uf2Path; placeholderText: "Or choose a local .uf2 file"; readOnly: true }
                                FlatButton { text: "Browse…"; onClicked: uf2FileDialog.open() }
                                FlatButton {
                                    text: "Install local"
                                    enabled: !device.updateBusy && window.uf2Path.length > 0 && device.unlocked
                                    onClicked: device.selectLocalUf2(window.uf2Path)
                                }
                            }
                        }
                    }

                    Rectangle {
                        radius: 7
                        color: palette.panel
                        border.color: palette.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12
                            Text { text: "Signed serial DFU package"; color: palette.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                text: "Choose a signed .zip package created by the project's signing workflow. The device must already be in its serial DFU bootloader; the current firmware's DFU command enters UF2 instead. Encrypted secure updates are not implemented yet."
                                color: palette.muted
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                            Text { Layout.fillWidth: true; text: "If nrfutil is not present, the app downloads and installs it automatically."; color: palette.muted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                            RowLayout {
                                Layout.fillWidth: true
                                ThemedTextField { Layout.fillWidth: true; text: window.nrfutilPackagePath; placeholderText: "Select a signed .zip package"; readOnly: true }
                                FlatButton { text: "Browse…"; onClicked: nrfutilFileDialog.open() }
                            }
                            FlatButton {
                                text: device.updateBusy ? "Updating…" : "Run nrfutil DFU"
                                enabled: !device.updateBusy && window.nrfutilPackagePath.length > 0 && device.connectionTarget.length > 0
                                fillColor: palette.green
                                hoverColor: palette.greenDeep
                                foreground: palette.canvas
                                onClicked: device.startNrfutilUpdate(window.nrfutilPackagePath)
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: 5
                    color: palette.panelRaised
                    visible: device.updateStatus.length > 0
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        BusyIndicator { running: device.updateBusy; visible: running; implicitWidth: 22; implicitHeight: 22 }
                        Text { Layout.fillWidth: true; text: device.updateStatus; color: palette.text; font.pixelSize: 12; elide: Text.ElideRight }
                    }
                }
            }
        }
    }

    Rectangle {
        id: copyToast
        property bool shown: false
        property string toastText: "Copied to clipboard"
        anchors.horizontalCenter: parent.horizontalCenter
        y: shown ? parent.height - height - 26 : parent.height + 12
        width: toastLabel.implicitWidth + 34
        height: 46
        radius: 12
        color: palette.panelRaised
        border.color: palette.border
        opacity: shown ? 1 : 0
        visible: opacity > 0
        z: 10
        Behavior on y { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: copyToast.toastText
            color: palette.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Timer {
            id: toastTimer
            interval: 3000
            onTriggered: copyToast.shown = false
        }
    }

    Connections {
        target: device
        function onNotificationRequested(message) {
            copyToast.toastText = message
            copyToast.shown = true
            toastTimer.restart()
        }
    }

    Dialog {
        id: addDialog
        title: "Add an account"
        modal: true
        anchors.centerIn: parent
        width: 440
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "Account name"; color: palette.muted; font.pixelSize: 12 }
            TextField { id: accountName; Layout.fillWidth: true; placeholderText: "e.g. Example: you@example.com"; selectByMouse: true }
            Text { text: "Base32 secret"; color: palette.muted; font.pixelSize: 12 }
            TextField { id: accountSecret; Layout.fillWidth: true; placeholderText: "Paste the secret key"; echoMode: TextInput.Password; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: addDialog.close() }
                FlatButton {
                    text: "Add account"
                    fillColor: palette.green
                    hoverColor: "#b1e987"
                    foreground: "#162112"
                    onClicked: {
                        if (device.addAccount(accountName.text, accountSecret.text)) {
                            accountName.clear()
                            accountSecret.clear()
                            addDialog.close()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: qrSourceDialog
        title: "Import authenticator QR"
        modal: true
        anchors.centerIn: parent
        width: 430
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 9; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "Choose where to scan the authenticator QR code."; color: palette.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: qrSourceDialog.close() }
                FlatButton { text: "From image…"; onClicked: { qrSourceDialog.close(); qrFileDialog.open() } }
                FlatButton {
                    text: "Use camera"
                    fillColor: palette.greenDeep
                    foreground: palette.text
                    onClicked: {
                        qrSourceDialog.close()
                        cameraScanDialog.open()
                        device.scanQrCamera()
                    }
                }
            }
        }
    }

    Dialog {
        id: cameraScanDialog
        title: "Scanning camera"
        modal: true
        anchors.centerIn: parent
        width: 430
        standardButtons: Dialog.NoButton
        onRejected: device.cancelQrScan()
        background: Rectangle { color: palette.panel; radius: 9; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text { Layout.fillWidth: true; text: "Camera is active and scanning for an authenticator QR code."; color: palette.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: cameraScanDialog.visible }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel scan"; onClicked: { device.cancelQrScan(); cameraScanDialog.close() } }
            }
        }
    }

    FileDialog {
        id: qrFileDialog
        title: "Choose an image containing an authenticator QR code"
        nameFilters: ["Image files (*.png *.jpg *.jpeg *.bmp *.webp)", "All files (*)"]
        onAccepted: device.scanQrImage(selectedFile.toString())
    }

    FileDialog {
        id: uf2FileDialog
        title: "Choose UF2 firmware"
        nameFilters: ["UF2 firmware (*.uf2)", "All files (*)"]
        onAccepted: window.uf2Path = selectedFile.toString()
    }

    FileDialog {
        id: nrfutilFileDialog
        title: "Choose signed nrfutil package"
        nameFilters: ["DFU package (*.zip)", "All files (*)"]
        onAccepted: window.nrfutilPackagePath = selectedFile.toString()
    }

    ListModel { id: qrPreviewModel }

    Dialog {
        id: qrDialog
        title: "Review imported accounts"
        modal: true
        anchors.centerIn: parent
        width: 690
        height: 520
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "Select accounts to add. Secrets are sent directly to the unlocked device."; color: palette.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "IMPORT"; color: palette.muted; font.pixelSize: 10; font.weight: Font.Bold }
                Item { Layout.fillWidth: true }
                Text { text: "ACCOUNT LABEL"; color: palette.muted; font.pixelSize: 10; font.weight: Font.Bold }
            }
            ListView {
                id: qrPreviewList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: qrPreviewModel
                spacing: 4
                delegate: RowLayout {
                    width: qrPreviewList.width
                    height: 48
                    CheckBox {
                        checked: model.enabled
                        onToggled: qrPreviewModel.setProperty(index, "enabled", checked)
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: model.username
                        onTextEdited: qrPreviewModel.setProperty(index, "username", text)
                    }
                    TextField {
                        Layout.preferredWidth: 210
                        text: model.secret
                        echoMode: TextInput.Password
                        onTextEdited: qrPreviewModel.setProperty(index, "secret", text)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: qrDialog.close() }
                FlatButton {
                    text: "Import selected"
                    fillColor: palette.green
                    hoverColor: "#b1e987"
                    foreground: "#162112"
                    onClicked: {
                        var selected = []
                        for (var index = 0; index < qrPreviewModel.count; ++index) {
                            var entry = qrPreviewModel.get(index)
                            if (entry.enabled)
                                selected.push({ "username": entry.username, "secret": entry.secret })
                        }
                        device.addAccounts(selected)
                        qrDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        title: "Delete account?"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 16
            Text { text: "This removes the account from the device vault."; color: palette.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: deleteDialog.close() }
                FlatButton { text: "Delete"; fillColor: "#873b36"; hoverColor: "#a94942"; onClicked: { device.deleteAccount(window.pendingDeleteId); deleteDialog.close() } }
            }
        }
    }

    Dialog {
        id: clearAccountsDialog
        title: "Clear all accounts?"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 14
            Text { text: "This removes every saved account from the encrypted vault."; color: palette.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: clearAccountsDialog.close() }
                FlatButton { text: "Clear accounts"; fillColor: "#873b36"; hoverColor: "#a94942"; onClicked: { device.clearAccounts(); clearAccountsDialog.close() } }
            }
        }
    }

    Dialog {
        id: clearCalibrationDialog
        title: "Clear calibration?"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 14
            Text { text: "This clears the aging offset and the stored calibration baseline."; color: palette.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: clearCalibrationDialog.close() }
                FlatButton { text: "Clear calibration"; fillColor: "#873b36"; hoverColor: "#a94942"; onClicked: { device.clearCalibration(); clearCalibrationDialog.close() } }
            }
        }
    }

    Dialog {
        id: factoryResetDialog
        title: "Erase this device?"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.NoButton
        background: Rectangle { color: palette.panel; radius: 8; border.color: palette.border }
        contentItem: ColumnLayout {
            spacing: 14
            Text { text: "Factory reset permanently erases all saved accounts and calibration settings."; color: palette.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton { text: "Cancel"; onClicked: factoryResetDialog.close() }
                FlatButton { text: "Erase vault"; fillColor: "#873b36"; hoverColor: "#a94942"; onClicked: { device.factoryReset(); factoryResetDialog.close() } }
            }
        }
    }

    Connections {
        target: device
        function onQrImportCompleted(accounts, error) {
            cameraScanDialog.close()
            if (error.length) {
                device.showMessage(error)
                return
            }
            qrPreviewModel.clear()
            for (var index = 0; index < accounts.length; ++index)
                qrPreviewModel.append({ "username": accounts[index].username, "secret": accounts[index].secret, "enabled": true })
            qrDialog.open()
        }
    }
}

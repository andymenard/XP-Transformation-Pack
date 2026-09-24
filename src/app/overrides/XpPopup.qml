// XPLasma: one notification as XP shows it -- a balloon from its icon in the
// tray, or, for a file transfer, XP's copy dialog (a transfer that fails
// becomes an error balloon). Globals decides which balloon is on screen:
// one at a time, oldest first.
import QtQuick

import org.kde.notificationmanager as NotificationManager

import "global"

QtObject {
    id: popup

    property int notificationId: -1
    property int type: NotificationManager.Notifications.NotificationType
    property string summary
    property string body
    property var image: null            // the sender's picture (a QImage), if it sent one
    property string iconName
    property string applicationIconName
    property string applicationName
    property string desktopEntry
    property int urgency: NotificationManager.Notifications.NormalUrgency
    property var actionNames: []
    property var actionLabels: []
    property bool hasDefaultAction: false
    property bool hasReplyAction: false
    property string replyPlaceholderText
    property string replySubmitButtonText
    property var urls: []
    property int timeout: -1            // -1: the default; 0: until closed
    property bool resident: false
    property int percentage: 0
    property int jobState: 0
    property int jobError: 0
    property var jobDetails: null
    property bool killable: false
    property bool recalled: false       // shown again from its blinking icon

    readonly property bool isJob: type === NotificationManager.Notifications.JobType
    readonly property bool finished: isJob && jobState === NotificationManager.Notifications.JobStateStopped
    readonly property bool failed: finished && jobError !== 0
    readonly property bool wantsBalloon: !isJob || failed
    readonly property bool shown: Globals.xpCurrentId === notificationId

    // XP's icons for how serious it is: a warning for critical ones, an
    // error when the sender says so (or a transfer failed), information
    // otherwise.
    readonly property string severity: {
        if (failed || (!isJob && jobError !== 0) || /error|dialog-error/.test(iconName))
            return "error";
        if (urgency === NotificationManager.Notifications.CriticalUrgency || /warning|caution/.test(iconName))
            return "warning";
        return "info";
    }
    // Its icon in the tray: the sending app's.
    readonly property string trayIcon: applicationIconName || iconName || "dialog-information"

    signal closeRequested()
    signal defaultActionRequested()
    signal actionRequested(string name)
    signal replyRequested(string text)
    signal expireRequested()
    signal killRequested()
    signal readRequested()

    property var balloon: null
    property var dialog: null
    property Component balloonComponent: Component { XpBalloon {} }
    property Component dialogComponent: Component { XpCopyDialog {} }

    // (Worked out here, not read from the properties above: called from
    // their change handlers, those can still hold the old state -- a
    // transfer just stopped with an error reads "finished, not failed".)
    function sync() {
        const done = isJob && jobState === NotificationManager.Notifications.JobStateStopped;
        const wants = !isJob || (done && jobError !== 0);
        if (wants && !recalled)
            Globals.xpEnqueue(popup);
        if (wants && !balloon)
            balloon = balloonComponent.createObject(null, {popup: popup});
        if (isJob && !done && !dialog)
            dialog = dialogComponent.createObject(null, {popup: popup});
        if (dialog && (!isJob || done)) {
            dialog.destroy();
            dialog = null;
        }
        // A transfer that finished well leaves nothing behind, as XP's.
        if (done && jobError === 0)
            Qt.callLater(() => {
                if (popup.finished && !popup.failed)
                    popup.closeRequested();
            });
        Globals.xpAdvance();
    }
    onWantsBalloonChanged: sync()
    onFinishedChanged: sync()
    onJobErrorChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: {
        if (!recalled)
            Globals.xpDropped(notificationId);
        if (balloon)
            balloon.destroy();
        if (dialog)
            dialog.destroy();
    }
}

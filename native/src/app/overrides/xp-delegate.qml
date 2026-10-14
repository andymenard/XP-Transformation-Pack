XpPopup {
            id: xpPopup
            // (Required, not context properties: Plasma 6.6's Globals.qml binds
            // its components -- pragma ComponentBehavior: Bound.)
            required property int index
            required property var model
            notificationId: model.notificationId
            type: model.type
            summary: model.summary || ""
            body: model.body || ""
            image: model.image || null
            iconName: model.iconName || ""
            applicationIconName: model.applicationIconName || ""
            applicationName: model.applicationName || ""
            desktopEntry: model.desktopEntry || ""
            urgency: model.urgency || NotificationManager.Notifications.NormalUrgency
            actionNames: model.actionNames || []
            actionLabels: model.actionLabels || []
            hasDefaultAction: model.hasDefaultAction || false
            hasReplyAction: model.hasReplyAction || false
            replyPlaceholderText: model.replyPlaceholderText || ""
            replySubmitButtonText: model.replySubmitButtonText || ""
            urls: model.urls || []
            timeout: model.timeout === undefined ? -1 : model.timeout
            resident: model.resident || false
            percentage: model.percentage || 0
            jobState: model.jobState || 0
            jobError: model.jobError || 0
            jobDetails: model.jobDetails || null
            killable: !!model.killable

            readonly property bool hasSomeActions: hasDefaultAction || actionLabels.length > 0 || hasReplyAction
            readonly property int behavior: model.resident ? NotificationManager.Notifications.None : NotificationManager.Notifications.Close

            onCloseRequested: globals.popupNotificationsModel.close(globals.popupNotificationsModel.index(index, 0))
            onDefaultActionRequested: globals.popupNotificationsModel.invokeDefaultAction(globals.popupNotificationsModel.index(index, 0), behavior)
            onActionRequested: name => globals.popupNotificationsModel.invokeAction(globals.popupNotificationsModel.index(index, 0), name, behavior)
            onReplyRequested: text => globals.popupNotificationsModel.reply(globals.popupNotificationsModel.index(index, 0), text, behavior)
            onKillRequested: globals.popupNotificationsModel.killJob(globals.popupNotificationsModel.index(index, 0))
            onReadRequested: {
                model.read = true;
                globals.xpForget(notificationId);
            }
            onExpireRequested: {
                globals.xpExpired(xpPopup);
                if (model.resident || hasSomeActions) {
                    // (kept in the history, usable, as Plasma's popups do)
                    model.expired = true;
                } else {
                    globals.popupNotificationsModel.expire(globals.popupNotificationsModel.index(index, 0));
                }
            }

            Component.onCompleted: {
                if (globals.inhibited) {
                    model.wasAddedDuringInhibition = false;
                }
                if (model.type === NotificationManager.Notifications.NotificationType && model.desktopEntry && !model.notifyRcName) {
                    globals.notificationSettings.registerKnownApplication(model.desktopEntry);
                    globals.notificationSettings.save();
                }
                // The balloon keeps its own time, from when it's shown.
                globals.popupNotificationsModel.stopTimeout(globals.popupNotificationsModel.index(index, 0));
            }
        }
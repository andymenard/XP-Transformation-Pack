XpPopup {
            id: xpPopup
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

            onCloseRequested: popupNotificationsModel.close(popupNotificationsModel.index(index, 0))
            onDefaultActionRequested: popupNotificationsModel.invokeDefaultAction(popupNotificationsModel.index(index, 0), behavior)
            onActionRequested: name => popupNotificationsModel.invokeAction(popupNotificationsModel.index(index, 0), name, behavior)
            onReplyRequested: text => popupNotificationsModel.reply(popupNotificationsModel.index(index, 0), text, behavior)
            onKillRequested: popupNotificationsModel.killJob(popupNotificationsModel.index(index, 0))
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
                    popupNotificationsModel.expire(popupNotificationsModel.index(index, 0));
                }
            }

            Component.onCompleted: {
                if (globals.inhibited) {
                    model.wasAddedDuringInhibition = false;
                }
                if (model.type === NotificationManager.Notifications.NotificationType && model.desktopEntry && !model.notifyRcName) {
                    notificationSettings.registerKnownApplication(model.desktopEntry);
                    notificationSettings.save();
                }
                // The balloon keeps its own time, from when it's shown.
                popupNotificationsModel.stopTimeout(popupNotificationsModel.index(index, 0));
            }
        }
/*
    XP's window sounds -- Open program, Close program, Minimize, Maximize,
    Restore Up and Restore Down -- which Plasma has no events for. The
    windows as Plasma's task model sees them (every desktop, every screen,
    ungrouped); each change sent as an event of XPlasma's notifyrc
    (xplasma.notifyrc, written with a sound scheme), which plays the sound
    theme's window-new, window-minimized... A window's states are learnt
    quietly at first, so a shell starting up doesn't open every window
    with a sound. Only the first screen's Start button plays them
    (`enabled`), so two taskbars don't play twice.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
import QtQuick
import org.kde.taskmanager as TaskManager
import org.kde.notification

Item {
    id: sounds

    // What each window was (minimized, maximized), by its first window id.
    property var known: ({})
    property bool ready: false

    function role(row, name) {
        return tasks.data(tasks.index(row, 0), TaskManager.AbstractTasksModel[name]);
    }
    function key(row) {
        const ids = sounds.role(row, "WinIdList");
        return ids && ids.length ? String(ids[0]) : "";
    }
    function isWindow(row) {
        return sounds.role(row, "IsWindow") === true && sounds.role(row, "IsStartup") !== true;
    }
    function state(row) {
        return {min: sounds.role(row, "IsMinimized") === true, max: sounds.role(row, "IsMaximized") === true};
    }
    function play(event) {
        if (!sounds.enabled || !sounds.ready)
            return;
        const n = eventComponent.createObject(sounds, {eventId: event});
        n.sendEvent();
        n.destroy(10000);
    }

    Component {
        id: eventComponent
        Notification {
            componentName: "xplasma"
            autoDelete: false
            flags: Notification.CloseOnTimeout
        }
    }

    TaskManager.TasksModel {
        id: tasks
        groupMode: TaskManager.TasksModel.GroupDisabled
        filterByVirtualDesktop: false
        filterByScreen: false
        filterByActivity: false
        filterNotMinimized: false
    }

    Connections {
        target: tasks
        function onRowsInserted(parent, first, last) {
            for (let row = first; row <= last; ++row) {
                const k = sounds.key(row);
                if (!k || !sounds.isWindow(row) || sounds.known[k] !== undefined)
                    continue;
                sounds.known[k] = sounds.state(row);
                sounds.play("windowOpen");
            }
        }
        function onRowsAboutToBeRemoved(parent, first, last) {
            for (let row = first; row <= last; ++row) {
                const k = sounds.key(row);
                if (!k || sounds.known[k] === undefined)
                    continue;
                delete sounds.known[k];
                sounds.play("windowClose");
            }
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            for (let row = topLeft.row; row <= bottomRight.row; ++row) {
                const k = sounds.key(row);
                if (!k || !sounds.isWindow(row))
                    continue;
                const was = sounds.known[k];
                const now = sounds.state(row);
                sounds.known[k] = now;
                if (was === undefined)
                    continue;
                if (!was.min && now.min)
                    sounds.play("windowMinimize");
                else if (was.min && !now.min)
                    sounds.play("windowRestoreUp");
                else if (!was.max && now.max)
                    sounds.play("windowMaximize");
                else if (was.max && !now.max)
                    sounds.play("windowRestoreDown");
            }
        }
    }

    // (the windows there already, learnt without a sound)
    Timer {
        interval: 3000
        running: true
        onTriggered: {
            for (let row = 0; row < tasks.count; ++row)
                if (sounds.key(row) && sounds.isWindow(row))
                    sounds.known[sounds.key(row)] = sounds.state(row);
            sounds.ready = true;
        }
    }
}

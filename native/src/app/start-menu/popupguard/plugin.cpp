// SPDX-License-Identifier: GPL-3.0-or-later
#include <QQmlExtensionPlugin>
#include <qqml.h>
#include <QWindow>
#include <QQuickItem>
#include <QPointer>
#include <QDebug>
class ResizeGuard : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    QPointer<QWindow> m_window;
    QList<QPointer<QObject>> m_filters;
    QList<QPair<QPointer<QQuickItem>, qreal>> m_backgrounds;
public:
    using QObject::QObject;
    ~ResizeGuard() override {restore();}
    QWindow* window() const {return m_window;}
    void restore(){
        if(m_window)for(auto filter:m_filters)if(filter)m_window->installEventFilter(filter);
        m_filters.clear();
        for(const auto &entry:m_backgrounds)if(entry.first)entry.first->setOpacity(entry.second);
        m_backgrounds.clear();
    }
    void setWindow(QWindow *window){
        if(window==m_window)return;
        restore();m_window=window;
        if(window)for(auto *child:window->children()){
            if(QByteArray(child->metaObject()->className())=="PlasmaQuick::WindowResizeHandler"){
                window->removeEventFilter(child);m_filters.append(child);
            }
        }
        if(window)for(auto *item:window->findChildren<QQuickItem*>()){
            if(QByteArray(item->metaObject()->className())=="PlasmaQuick::DialogBackground"){
                m_backgrounds.append(qMakePair(QPointer<QQuickItem>(item),item->opacity()));
                item->setOpacity(0);
            }
        }
        if(window && m_backgrounds.isEmpty())qWarning()<<"XP Start menu: no Plasma background layer found";
        if(window && m_filters.isEmpty())qWarning()<<"XP Start menu: no Plasma resize handler found";
        emit windowChanged();
    }
signals:
    void windowChanged();
};
class Plugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override {qmlRegisterType<ResizeGuard>(uri,1,0,"ResizeGuard");}
};
#include "plugin.moc"

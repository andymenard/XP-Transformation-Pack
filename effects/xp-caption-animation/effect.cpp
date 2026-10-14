// SPDX-License-Identifier: GPL-3.0-or-later
// Independent KWin implementation of classic caption motion. Behavioral
// reference: aubymori's Windhawk classic-min-max-animations (250ms, linear,
// constant caption height; icon and text without caption buttons).
#include "caption.h"
#include <effect/offscreeneffect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/imageitem.h>
#ifdef XP_KWIN_66
#include <scene/itemrenderer.h>
#endif
#include <scene/windowitem.h>
#include <scene/workspacescene.h>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QHash>

namespace KWin {
class XPCaptionAnimation : public CrossFadeEffect {
    Q_OBJECT
public:
    XPCaptionAnimation() {
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_CONSTRUCT\n");
        #ifdef XP_KWIN_66
        m_caption=effects->scene()->renderer()->createImageItem(effects->scene()->overlayItem());
#else
        m_caption=std::make_unique<ImageItem>(effects->scene()->overlayItem());
#endif
        m_caption->setVisible(false);m_caption->setZ(1000);
        m_timer.setInterval(16);m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer,&QTimer::timeout,this,[this]{tick();});
        connect(effects,&EffectsHandler::windowAdded,this,[this](EffectWindow *w){manage(w);});
        connect(effects,&EffectsHandler::windowClosed,this,[this](EffectWindow *w){if(w==m_window)finish();m_before.remove(w);});
        connect(effects,&EffectsHandler::windowDeleted,this,[this](EffectWindow *w){if(w==m_window)finish();m_before.remove(w);});
        for(auto *w:effects->stackingOrder())manage(w);
    }
    ~XPCaptionAnimation() override {finish();}
    bool isActive() const override {return !m_window.isNull();}
    void prePaintWindow(RenderView *view,EffectWindow *w,WindowPrePaintData &data
#ifdef XP_KWIN_66
        ,std::chrono::milliseconds presentTime
#endif
    ) override {
        if(w==m_window){data.setTransformed();data.setTranslucent();}
        CrossFadeEffect::prePaintWindow(view,w,data
#ifdef XP_KWIN_66
            ,presentTime
#endif
        );
    }
    void paintWindow(const RenderTarget &target,const RenderViewport &viewport,EffectWindow *w,int mask,const Region &region,WindowPaintData &data) override {
        if(w==m_window){
            if(m_restoreMin)return; // Body becomes visible after the moving caption arrives.
            const QRectF now=w->frameGeometry();
            data.setCrossFadeProgress(0); // Hold the pre-transition snapshot, no crossfade.
            data.setXScale(m_oldGeometry.width()/qMax(qreal(1),now.width()));
            data.setYScale(m_oldGeometry.height()/qMax(qreal(1),now.height()));
            data.setXTranslation(m_oldGeometry.x()-now.x());
            data.setYTranslation(m_oldGeometry.y()-now.y());
        }
        CrossFadeEffect::paintWindow(target,viewport,w,mask,region,data);
    }
private:
    struct Before {QRectF geometry;QRectF caption;};
    QHash<EffectWindow*,Before> m_before;
    QPointer<EffectWindow> m_window;
    std::unique_ptr<ImageItem> m_caption;
    QRectF m_from,m_to,m_oldGeometry;
    QElapsedTimer m_clock;QTimer m_timer;
    bool m_minVisible=false,m_restoreMin=false;
    QString m_title;QIcon m_icon;QFont m_font;
    QColor m_left,m_right,m_text; qreal m_scale=1;
    bool eligible(EffectWindow *w) const {return w&&w->hasDecoration()&&!effects->activeFullScreenEffect();}
    QRectF captionRect(EffectWindow *w) const {
        const QRectF frame=w->frameGeometry(),inner=w->contentsRect();
        const qreal edge=qMax(qreal(0),inner.x());
        return {frame.x()+edge,frame.y()+edge,qMax(qreal(1),frame.width()-2*edge),qBound(qreal(16),inner.y()-edge,qreal(48))};
    }
    void manage(EffectWindow *w){
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_MANAGE decorated=%d\n",w->hasDecoration());
        connect(w,&EffectWindow::minimizedChanged,this,[this,w]{
            if(!eligible(w))return;
            QRectF task=w->iconGeometry();
            if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST")&&task.isEmpty())task=QRectF(300,700,160,30);
            if(task.width()<1||task.height()<1)return;
            const QRectF bar=captionRect(w);QRectF taskBar(task.x(),task.y(),task.width(),bar.height());
            begin(w,w->isMinimized()?bar:taskBar,w->isMinimized()?taskBar:bar,w->frameGeometry(),!w->isMinimized());
        });
        connect(w,&EffectWindow::windowMaximizedStateAboutToChange,this,[this,w]{
            if(!eligible(w)||w->isMinimized())return;
            finish();m_before.insert(w,{w->frameGeometry(),captionRect(w)});redirect(w);
        });
        connect(w,&EffectWindow::windowMaximizedStateChanged,this,[this,w]{
            if(!m_before.contains(w))return;
            const Before old=m_before.take(w);const QRectF to=captionRect(w);
            if(old.geometry==QRectF(w->frameGeometry())){unredirect(w);return;}
            begin(w,old.caption,QRectF(to.x(),to.y(),to.width(),old.caption.height()),old.geometry,false,true);
        });
    }
    void begin(EffectWindow *w,QRectF from,QRectF to,QRectF old,bool restore,bool snapshotted=false){
        if(m_window)finish();
        m_minVisible=w->isMinimized();
        if(m_minVisible)w->windowItem()->refVisible(WindowItem::PAINT_DISABLED_BY_MINIMIZE);
        if(!snapshotted)redirect(w);
        m_window=w;m_oldGeometry=old;m_from=from;m_to=to;m_restoreMin=restore;
        const auto config=KSharedConfig::openConfig(QStringLiteral("kdeglobals"));config->reparseConfiguration();KConfigGroup wm(config,QStringLiteral("WM"));
        m_left=wm.readEntry("activeBackground",QColor(0,84,227));m_right=wm.readEntry("activeBlend",QColor(61,149,255));m_text=wm.readEntry("activeForeground",QColor(Qt::white));
        m_font=wm.readEntry("activeFont",QFont(QStringLiteral("Trebuchet MS"),10,QFont::Bold));m_title=w->caption();m_icon=w->icon();
        m_scale=w->screen()?w->screen()->scale():1;
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_START %s %g,%g -> %g,%g h=%g\n",restore?"restore":(m_minVisible?"minimize":"maximize"),from.x(),from.y(),to.x(),to.y(),from.height());
        m_caption->setVisible(true);m_clock.start();m_timer.start();tick();
    }
    void tick(){
        if(!m_window||effects->activeFullScreenEffect()||m_clock.elapsed()>=XP::duration){finish();return;}
        const QRectF bar=XP::interpolate(m_from,m_to,qreal(m_clock.elapsed())/XP::duration);
        m_caption->setImage(XP::caption(bar.width(),bar.height(),m_scale,m_title,m_icon,m_font,m_left,m_right,m_text));
        m_caption->setPosition(bar.topLeft());m_caption->setSize(bar.size());effects->addRepaintFull();
    }
    void finish(){
        if(m_window&&qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_END elapsed=%lld\n",m_clock.elapsed());
        m_timer.stop();m_caption->setVisible(false);
        // Clear ownership before releasing references: a closing window can
        // emit windowDeleted synchronously while its final reference drops.
        QPointer<EffectWindow> window=m_window;const bool visibleRef=m_minVisible;
        m_window=nullptr;m_minVisible=false;m_restoreMin=false;
        if(window&&visibleRef)window->windowItem()->unrefVisible(WindowItem::PAINT_DISABLED_BY_MINIMIZE);
        if(window)unredirect(window);
        effects->addRepaintFull();
    }
};
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(XPCaptionAnimation,"metadata.json",return effects->isOpenGLCompositing();,return false;)
}
#include "effect.moc"

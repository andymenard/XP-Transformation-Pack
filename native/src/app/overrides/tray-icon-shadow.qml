
            // Presentation only: preserve the native icon pixels and hit area.
            layer.enabled: root.xpTrayAvailable && !root.vertical && abstractItem.inVisibleLayout
                && GraphicsInfo.api !== GraphicsInfo.Software
            layer.smooth: false
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.35
                shadowBlur: 0.15
                blurMax: 4
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 1
                autoPaddingEnabled: true
            }

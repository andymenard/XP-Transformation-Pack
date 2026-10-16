    Text {
        objectName: "xplasma-caption-shadow"
        // A second native-rendered glyph mask, exactly offset, with no blur.
        property var spec: root.xpShadow(auroraeTheme.themeName)
        visible: color.a > 0
        x: caption.x + (spec.x || 0)
        y: caption.y + (spec.y || 0)
        width: caption.width
        height: caption.height
        text: caption.text
        textFormat: caption.textFormat
        font: caption.font
        renderType: caption.renderType
        horizontalAlignment: caption.horizontalAlignment
        verticalAlignment: caption.verticalAlignment
        elide: caption.elide
        color: (decoration.client.active ? spec.active : spec.inactive) || "transparent"
    }

#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 tint;
    float tone;
    float glyph;
    vec4 edgeTint;
    float edgeWidth;
    float edgeTone;
} ubuf;
layout(binding=1) uniform sampler2D source;
void main() {
    vec4 p=texture(source,qt_TexCoord0);
    vec3 rgb=p.a>0.0 ? p.rgb/p.a : vec3(0.0);
    float value=dot(rgb,vec3(0.2126,0.7152,0.0722));
    float edge=0.0;
    if(ubuf.edgeWidth>0.0 && qt_TexCoord0.x>1.0-ubuf.edgeWidth) {
        float hi=max(rgb.r,max(rgb.g,rgb.b)),lo=min(rgb.r,min(rgb.g,rgb.b));
        float delta=hi-lo;
        if(delta>0.01 && hi>0.0 && delta/hi>0.12) {
            float hue=hi==rgb.g ? (rgb.b-rgb.r)/delta+2.0 : hi==rgb.b ? (rgb.r-rgb.g)/delta+4.0 : (rgb.g-rgb.b)/delta;
            edge=clamp((hue*60.0-125.0)/95.0,0.0,1.0);
        }
    }
    vec3 target=mix(ubuf.tint.rgb,ubuf.edgeTint.rgb,edge);
    float center=clamp(mix(ubuf.tone,ubuf.edgeTone,edge),0.05,0.95);
    vec3 mapped=value<center ? target*pow(value/center,1.1)
        : mix(target,vec3(1.0),0.65*(value-center)/(1.0-center));
    if(ubuf.glyph>0.5) {fragColor=vec4(ubuf.tint.rgb*p.a*ubuf.tint.a,p.a*ubuf.tint.a)*ubuf.qt_Opacity;return;}
    fragColor=vec4(mapped*p.a,p.a)*ubuf.qt_Opacity;
}

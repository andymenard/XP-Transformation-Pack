#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 tint;
    vec4 foreground;
    float tone;
    float button;
    float closeButton;
} ubuf;
layout(binding=1) uniform sampler2D source;
void main() {
    vec4 p=texture(source,qt_TexCoord0);
    vec3 rgb=p.a>0.0?p.rgb/p.a:vec3(0.0);
    float hi=max(rgb.r,max(rgb.g,rgb.b)),lo=min(rgb.r,min(rgb.g,rgb.b));
    bool glyph=ubuf.button>0.5 && all(greaterThan(qt_TexCoord0,vec2(0.2))) && all(lessThan(qt_TexCoord0,vec2(0.8)))
        && (hi==0.0 || (hi-lo)/hi<0.12) && (hi>0.96 || hi<0.06);
    if(glyph){
        if(ubuf.foreground.a>0.0)p=vec4(ubuf.foreground.rgb*p.a,p.a);
        fragColor=p*ubuf.qt_Opacity;return;
    }
    if(ubuf.closeButton>0.5 && rgb.r>rgb.g*1.2 && rgb.r>rgb.b*1.15){fragColor=p*ubuf.qt_Opacity;return;}
    // Match the CPU LUT for frame surfaces; buttons retain continuous luma.
    float value=ubuf.button>0.5?dot(rgb,vec3(0.2126,0.7152,0.0722))
        :floor((dot(floor(rgb*255.0+0.5),vec3(54.0,183.0,19.0))+128.0)/256.0)/255.0;
    float center=clamp(ubuf.tone,0.05,0.95);
    vec3 mapped=value<center?ubuf.tint.rgb*pow(value/center,1.1)
        :mix(ubuf.tint.rgb,vec3(1.0),0.65*(value-center)/(1.0-center));
    fragColor=vec4(mapped*p.a,p.a)*ubuf.qt_Opacity;
}

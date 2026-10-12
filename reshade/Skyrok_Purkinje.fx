// Skyrok - Night vision (Purkinje effect)
// Version: 1.0
// (Skyrok's plugin reads the line above: it replaces the copy in ReShade's folder only when the
//  version it ships is newer. Raise it whenever this file changes.)
//
// In the dark the eye stops using its colour-sensing cones and relies on its rods. Rods see no
// colour, respond best to blue-green light and hardly at all to red: the night looks grey-blue,
// red things go dark, and anything bright enough (a fire, a torch, a lit window) keeps its colour
// because it still wakes the cones up.
//
// Most night-vision shaders guess how dark it is from the picture itself. This one is told by the
// Skyrok SKSE plugin: outdoors it follows the night, indoors the darkness of the room's own
// lighting. So it does not switch on in a shadowed alley at noon, nor off when a torch is lit.
//
// Original code written for Skyrok - A Weather Mod. No third-party shader code is used.

#include "ReShade.fxh"

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Strength";
    ui_tooltip = "How far vision shifts to the grey-blue of night in full darkness.";
> = 0.6;

uniform float BlueShift <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Blue shift";
    ui_tooltip = "0 = plain grey night vision, 1 = strongly blue.";
> = 0.5;

uniform float LightProtection <
    ui_type = "slider";
    ui_min = 0.05; ui_max = 1.0;
    ui_label = "Light protection";
    ui_tooltip = "Brightness above which things keep their full colour (fires, torches, windows). "
                 "Lower protects more of the picture.";
> = 0.35;

uniform bool Preview <
    ui_label = "Always dark (preview)";
    ui_tooltip = "Shows the effect at full darkness even by day. Leave off for normal play.";
> = false;

// 0 in daylight and bright rooms, 1 at night and in dark places; -1 when the plugin is not connected
uniform float SkyrokDarkness < source = "skyrok_darkness"; > = -1.0;

// ---------------------------------------------------------------------------------------------

// Screen colours are stored gamma-encoded; light adds up in linear values.
float3 ToLinear(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 ToScreen(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

float3 PurkinjePS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 screen = tex2D(ReShade::BackBuffer, uv).rgb;

    float darkness = Preview ? 1.0 : saturate(SkyrokDarkness);
    float amount = darkness * Strength;
    if (amount <= 0.001)
        return screen;

    float3 light = ToLinear(screen);

    // What the cones see (ordinary brightness) and what the rods see: almost blind to red,
    // most sensitive between green and blue.
    float cones = dot(light, float3(0.2126, 0.7152, 0.0722));
    float rods = dot(light, float3(0.04, 0.56, 0.40));

    // Bright enough light still reaches the cones, so it keeps its colour. The threshold is given
    // on the screen's own scale, which is how a slider is judged by eye.
    float seen = pow(cones, 1.0 / 2.2);
    float coneShare = smoothstep(LightProtection * 0.35, LightProtection, seen);

    // The rod picture: no colour of its own, read by the brain as grey to blue. The tint is
    // scaled so that it does not change how bright a neutral grey looks.
    float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(0.55, 0.9, 1.6), BlueShift);
    tint /= dot(tint, float3(0.2126, 0.7152, 0.0722));
    float3 night = rods * tint;

    float shift = amount * (1.0 - coneShare);
    return ToScreen(lerp(light, night, shift));
}

technique Skyrok_Purkinje <
    ui_label = "Skyrok - Night vision (Purkinje)";
    ui_tooltip = "Grey-blue night vision in the dark, with red going dark and lights keeping their colour. "
                 "Driven by the Skyrok plugin: outdoors at night and in dark interiors.";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = PurkinjePS;
    }
}

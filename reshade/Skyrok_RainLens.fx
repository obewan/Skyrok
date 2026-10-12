// Skyrok - Rain on the lens
// Version: 1.0
// (Skyrok's plugin reads the line above: it replaces the copy in ReShade's folder only when the
//  version it ships is newer. Raise it whenever this file changes.)
// Water drops on the screen while it rains, as if on the face and not on a window: nothing
// stays put. Small drops creep down and dry out, heavier ones run down leaving a short trail.
// Each drop bends the picture behind it.
//
// Original code written for Skyrok - A Weather Mod. No third-party shader code is used.
//
// "SkyrokRain" is filled in by the Skyrok SKSE plugin when it is connected to ReShade: 0 when
// dry or indoors, up to 1 in a storm. Without the plugin it stays at -1 and only the preview
// checkbox shows the effect.

#include "ReShade.fxh"

uniform float RainAmount <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Rain amount";
    ui_tooltip = "How wet the lens gets in the heaviest rain.";
> = 0.6;

uniform bool ManualRain <
    ui_label = "Always raining (preview)";
    ui_tooltip = "Shows the drops even when Skyrok does not report rain. Leave off for normal play.";
> = false;

uniform float DropScale <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 2.0;
    ui_label = "Drop size";
> = 1.0;

uniform float Refraction <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0;
    ui_label = "Refraction";
    ui_tooltip = "How strongly a drop bends the picture behind it.";
> = 1.0;

uniform float RunSpeed <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 3.0;
    ui_label = "Sliding speed";
    ui_tooltip = "How fast the drops slide down. 0 keeps them still, like on a window.";
> = 1.0;

uniform float Highlight <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Highlights";
> = 0.35;

// 0..1 while it rains outdoors, 0 otherwise; -1 when the plugin is not connected
uniform float SkyrokRain < source = "skyrok_rain"; > = -1.0;

uniform float Timer < source = "timer"; >;

// ---------------------------------------------------------------------------------------------

float Rand(float2 p)
{
    return frac(sin(dot(p, float2(41.37, 289.13))) * 37519.73);
}

float3 Rand3(float2 p)
{
    return float3(Rand(p), Rand(p + 17.31), Rand(p + 53.77));
}

// A drop seen from the front. `d` is the distance to its centre in drop radii.
// Returns the coverage in x and how hard the surface tilts in y (0 at the centre, 1 at the rim).
float2 DropProfile(float d)
{
    float cover = 1.0 - smoothstep(0.75, 1.0, d);
    float tilt = saturate(d);
    return float2(cover, tilt * tilt);
}

// Whether a cell holds a drop, given the share of cells that should. It is a ramp, not a switch:
// as the rain builds up or dies down, drops grow in and shrink away instead of popping.
float Present(float cellRnd, float share)
{
    return saturate((share - cellRnd) / max(share * 0.35, 0.0002));
}

// Small drops. Each grid cell may hold one; it swells in, creeps down the skin and dries out.
// Every column creeps at its own uneven pace, so neighbours never move together.
// Returns the bend direction in xy and the coverage in z.
float3 Beads(float2 uv, float aspect, float time, float cells, float amount, float seed)
{
    float column = floor(uv.x * aspect * cells);
    float columnRnd = Rand(float2(column, seed + 2.0));
    float creep = lerp(0.02, 0.045, columnRnd) * RunSpeed;
    // holds, then lets go: a slow slide with a wobble in its speed
    float slide = time * creep + 0.004 * RunSpeed * sin(time * lerp(0.5, 1.4, columnRnd) + columnRnd * 30.0);

    // uv.y grows downward, so subtracting moves the drops down the screen
    float2 grid = float2(uv.x * aspect, uv.y - slide + columnRnd * 3.0) * cells;
    float2 cell = floor(grid);
    float2 local = frac(grid) - 0.5;
    float3 rnd = Rand3(cell + seed);

    // more rain fills more cells
    float present = Present(rnd.z, amount);

    float life = frac(time * lerp(0.05, 0.14, rnd.x) + rnd.y * 9.0);
    float size = smoothstep(0.0, 0.08, life) * (1.0 - smoothstep(0.6, 1.0, life)) * present;

    float2 centre = (rnd.xy - 0.5) * 0.5;
    float radius = lerp(0.10, 0.24, Rand(cell + seed + 5.5)) * size;

    // a sliding drop is a little longer than wide
    float2 toCentre = (local - centre) * float2(1.0, 0.85);
    float d = length(toCentre) / max(radius, 0.0001);
    float2 profile = DropProfile(d);

    float cover = profile.x * step(0.001, radius);
    return float3(normalize(toCentre + 0.00001) * profile.y * cover, cover);
}

// Heavier drops running down. The screen is cut into columns; each column carries drops at its
// own speed. A drop holds, lets go, holds again, and drags a short thinning trail behind it.
float3 Runners(float2 uv, float aspect, float time, float columns, float amount, float seed)
{
    const float rows = 2.2;

    float column = floor(uv.x * columns);
    float columnRnd = Rand(float2(column, seed));
    float speed = lerp(0.22, 0.5, columnRnd) * RunSpeed;

    // the pattern slides down the screen (uv.y grows downward)
    float2 grid = float2(uv.x * columns, uv.y * rows - time * speed + columnRnd * 31.0);
    float2 cell = float2(column, floor(grid.y));
    float3 rnd = Rand3(cell + seed);

    // stretch the cell so one unit is the same length across and down the screen
    float tall = columns / (rows * aspect);
    float2 local = float2(frac(grid.x) - 0.5, (frac(grid.y) - 0.5) * tall);

    float present = Present(rnd.z, amount);

    // the path wanders with the height on screen, so a trail follows the path its drop took
    float path = (rnd.x - 0.5) * 0.45 + 0.06 * sin(uv.y * 23.0 + columnRnd * 40.0);

    // Hold and let go: the drop sways around its place in the sliding pattern. The sway is kept
    // slower than the slide, so the drop slows down and speeds up but never climbs or jumps.
    float headY = 0.25 * tall * sin(time * speed * 2.5 + rnd.x * 6.2832);

    float radius = lerp(0.13, 0.22, rnd.y) * present;
    float2 toHead = float2(local.x - path, (local.y - headY) * 0.8);
    float2 head = DropProfile(length(toHead) / max(radius, 0.0001));
    float headCover = head.x * step(0.001, radius);

    // the trail: above the head, narrowing and breaking up as it dries. Skin holds less water
    // than glass, so it is short.
    float above = headY - local.y;
    float trailLength = 0.22 * tall;
    float along = saturate(above / trailLength);
    float width = radius * 0.5 * (1.0 - along);
    float across = (local.x - path) / max(width, 0.0001);
    float broken = 0.55 + 0.45 * sin(local.y * 34.0 + rnd.z * 60.0);
    float trailCover = (1.0 - smoothstep(0.6, 1.0, abs(across))) * step(0.0, above) * step(above, trailLength)
                     * (1.0 - along) * broken * present;

    float2 bend = normalize(toHead + 0.00001) * head.y * headCover
                + float2(clamp(across, -1.0, 1.0) * 0.6, 0.0) * trailCover * (1.0 - headCover);
    return float3(bend, saturate(headCover + trailCover * 0.8));
}

float3 RainLensPS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 scene = tex2D(ReShade::BackBuffer, uv).rgb;

    // light rain wets the lens less than a storm
    float amount = RainAmount * (ManualRain ? 1.0 : max(SkyrokRain, 0.0));
    if (amount <= 0.001)
        return scene;

    float aspect = BUFFER_WIDTH * BUFFER_RCP_HEIGHT;
    float time = Timer * 0.001;
    float scale = 1.0 / DropScale;

    // A face only carries a handful of drops. The last number of each line is the share of grid
    // cells that hold a drop at full rain: raise them for a wetter lens.
    float3 drops = Beads(uv, aspect, time, 18.0 * scale, amount * 0.05, 3.0)
                 + Beads(uv, aspect, time * 1.3, 34.0 * scale, amount * 0.012, 11.0)
                 + Runners(uv, aspect, time, 15.0 * scale * aspect / 1.78, amount * 0.22, 5.0)
                 + Runners(uv, aspect, time * 1.17, 24.0 * scale * aspect / 1.78, amount * 0.11, 23.0);

    float cover = saturate(drops.z);
    float2 bend = drops.xy;

    // a drop is a small lens: it shows the scene from further out, flipped around its centre
    float2 lensUv = uv - bend * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT) * 42.0 * Refraction * DropScale;
    float3 through = tex2D(ReShade::BackBuffer, lensUv).rgb;

    // rims take a little light away, and the side facing up and left catches a glint
    float rim = saturate(length(bend));
    through *= 1.0 - 0.22 * rim;
    float glint = saturate(dot(normalize(float3(bend, 0.55)), normalize(float3(-0.45, -0.65, 0.6))));
    through += pow(glint, 14.0) * Highlight * 0.5 * cover;

    return lerp(scene, through, cover);
}

technique Skyrok_RainLens <
    ui_label = "Skyrok - Rain on the lens";
    ui_tooltip = "Water drops on the screen while it rains. Driven by the Skyrok plugin when connected.";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = RainLensPS;
    }
}

"""Writes the preset ini files (nights and interiors, Darker / Super darker) next to this script."""
from pathlib import Path

PRESETS = {
    # values measured from the v1.92 darkness plugins (median ratio against vanilla)
    'Darker': dict(Sky=0.56, Fog=0.49, Ambient=0.44, Sunlight=0.33, EffectLighting=0.36, SkyStatics=0.58,
                   Water=0.33, Stars=0.80, Clouds=0.57, TwAmbient=0.73, TwClouds=0.83),
    'SuperDarker': dict(Sky=0.42, Fog=0.30, Ambient=0.25, Sunlight=0.10, EffectLighting=0.15, SkyStatics=0.43,
                        Water=0.10, Stars=0.80, Clouds=0.42, TwAmbient=0.64, TwClouds=0.77),
}

# Weathers the v1.92 darkness plugins left alone: interior "FX" weathers and special places.
EXCLUDE = """
Skyrim.esm|0x05ED7A = FXWthrInvertLightsSolitude
Skyrim.esm|0x06ED5A = FXWthrInvertDayNight
Skyrim.esm|0x06ED5B = FXWthrCaveBlueSkylight
Skyrim.esm|0x07548F = FXWthrSunlight
Skyrim.esm|0x075491 = FXWthrSunlightWhite
Skyrim.esm|0x075DE5 = FXWthrCaveBluePaleLight
Skyrim.esm|0x0777CF = FXWthrInvertDayNighWarm
Skyrim.esm|0x08277A = FXWthrInvertWindowsWhiterun
Skyrim.esm|0x08282A = FXWthrInvertLightsWhiterun
Skyrim.esm|0x0AEE84 = FXWthrInvertWindowsWindhelm
Skyrim.esm|0x0D4886 = FXMagicStormRain
Skyrim.esm|0x0ECC96 = FXWthrInvertWindowsWindhelm2
Skyrim.esm|0x101910 = FXWthrInvertLightMarkarth
Skyrim.esm|0x10C32F = FXSkyrimStormBlowingGrass
Skyrim.esm|0x10DA13 = FXWthrInvertWindowsWinterhold
Skyrim.esm|0x10FEF8 = SovngardeDark
Skyrim.esm|0x10D9EC = SovngardeClear
Skyrim.esm|0x105945 = SolitudeBluePalaceFog
Skyrim.esm|0x105944 = SolitudeBluePalaceFogNMARE
Skyrim.esm|0x105943 = SolitudeBluePalaceFogFEAR
Skyrim.esm|0x10199F = SkyrimMQ206weather
Skyrim.esm|0x048C14 = BlackreachWeather
Skyrim.esm|0x0A6858 = WorldMapWeather
Skyrim.esm|0x02E7AB = TESTCloudyRain
Dawnguard.esm|0x014551 = SoulCairnAmb01_Rain
Dawnguard.esm|0x018DBD = SoulCairnAmb04
Dawnguard.esm|0x018DBC = SoulCairnAmb03
Dawnguard.esm|0x018DBB = SoulCairnAmb02
Dawnguard.esm|0x00959F = SoulCairnAurora
Dawnguard.esm|0x001407 = SoulCairnAmb01
Dragonborn.esm|0x034CFB = DLC2ApocryphaWeatherNew
Dragonborn.esm|0x01DFF5 = DLC2ApocryphaWeather
""".strip()

TEMPLATE = """; Skyrok - A Weather Mod: night darkness settings ({name} preset)
; Every value is a multiplier applied to the colours of ALL loaded weathers,
; including the ones added or changed by other weather mods.
; 1.0 = unchanged, 0.5 = half as bright, 0 = black. Restart the game after editing.

[General]
Enabled = true

[Night]
Sky = {Sky}
Fog = {Fog}
Ambient = {Ambient}
Sunlight = {Sunlight}
EffectLighting = {EffectLighting}
SkyStatics = {SkyStatics}
Water = {Water}
Stars = {Stars}
Clouds = {Clouds}

; sunrise and sunset
[Twilight]
Ambient = {TwAmbient}
Clouds = {TwClouds}

; Moonlit nights: the night values above follow the moon phase.
; With Strength = 0.30 a clear full-moon night is 30% brighter than the values above and a
; new-moon night 30% darker; the effect is weaker under clouds, rain or snow.
[Moonlight]
Enabled = true
Strength = 0.30

; Weathers left untouched: Plugin|0xFormID = any note
[Exclude]
{exclude}
"""

INTERIOR_PRESETS = {
    # The v1.92 interior plugins averaged about x0.75 on ambient (grey 30 against a vanilla median of 40);
    # these presets go a step darker after in-game testing.
    # Those plugins also set the directional light to a fixed grey 15 (about x0.19) and removed fog entirely;
    # here every interior keeps its own colours and fog and is darkened in proportion instead.
    'InteriorsDarker': dict(Ambient=0.48, Directional=0.22, Fog=0.40),
    'InteriorsSuperDarker': dict(Ambient=0.30, Directional=0.10, Fog=0.20),
}

# Other realms and dream places, left untouched (the v1.92 plugins did not edit them either).
INTERIOR_EXCLUDE = """
Skyrim.esm|0x0487E6 = BlackreachTemplate
Skyrim.esm|0x0A2682 = NightCallerDreamTemplate
Dragonborn.esm|0x034CE1 = DLC2ApocryphaWorldLightingTemplate
Dragonborn.esm|0x034CE0 = DLC2ApocryphaDarkTemplate
Dragonborn.esm|0x034C19 = DLC2ApocryphaNewLightingTemplate
Dragonborn.esm|0x019A81 = DLC2ApocryphaLightingTemplate
""".strip()

INTERIOR_TEMPLATE = """; Skyrok - A Weather Mod: interior darkness settings ({name} preset)
; Every value is a multiplier applied to ALL lighting templates and ALL interior cells,
; including the ones added or changed by other mods, so every interior is darkened the same way.
; 1.0 = unchanged, 0.5 = half as bright, 0 = black. Restart the game after editing.

[General]
Enabled = true

[Interior]
; overall light level of the room
Ambient = {Ambient}
; the main directional light (windows, skylights)
Directional = {Directional}
; fog colour: fog adds light to a dark room, so it is dimmed along with the rest
Fog = {Fog}
; lights never start fading closer than this distance (0 = leave the distances alone)
LightFadeStartMin = 15000
LightFadeEndMin = 20000

; Lighting templates left untouched, with the cells that use them: Plugin|0xFormID = any note
[Exclude]
{exclude}
"""

IMAGE_SPACE_PRESETS = {
    # Brightness and contrast are the v1.92 preset values. Saturation is the v1.92 value x 0.7:
    # it is now the saturation seen on screen, and vanilla sits around 0.55 (nights) to 0.85 (interiors).
    'ImageSpaceOptimistic': dict(Saturation=0.70, Brightness=1.1, Contrast=1.2),
    'ImageSpaceRealistic': dict(Saturation=0.63, Brightness=1.0, Contrast=1.3),
    'ImageSpaceNeutral': dict(Saturation=0.70, Brightness=1.0, Contrast=1.0),
    'ImageSpaceBright': dict(Saturation=0.84, Brightness=1.2, Contrast=1.2),
    'ImageSpaceDark': dict(Saturation=0.56, Brightness=0.8, Contrast=1.2),
}

# Special effects and other realms, left untouched (the v1.92 presets did not edit them either).
IMAGE_SPACE_EXCLUDE = """
Dawnguard.esm|0x00F675 = DLC1_UnderwaterImageSpace
Dawnguard.esm|0x0181C4 = DLC01UnderLava01
Dawnguard.esm|0x008941 = DLC1EclipseISNight
Dawnguard.esm|0x006AED = DLC1EclipseIS
Dawnguard.esm|0x007AD7 = ISDLC01SoulCairn
Dragonborn.esm|0x034CFA = DLC2ISApocryphaWorld
Dragonborn.esm|0x01C48C = DLC2ISApocrypha
""".strip()

IMAGE_SPACE_TEMPLATE = """; Skyrok - A Weather Mod: image space settings ({name} preset)
; The same grading is applied to ALL image spaces, including the ones added or changed by other mods.
; 0 leaves a value as the game or other mods set it. Restart the game after editing.

[General]
Enabled = true

[ImageSpace]
; Saturation as seen on screen: 1.0 = full colour, 0 = leave alone.
; The plugin compensates for each image space's own tint, so fires and lights keep their colour at night.
Saturation = {Saturation}
Brightness = {Brightness}
Contrast = {Contrast}
; shorter, weaker eye adaptation (being blinded after looking at a bright light)
EyeAdaptSpeed = 80
EyeAdaptStrength = 1

; Image spaces left untouched: Plugin|0xFormID = any note
[Exclude]
{exclude}
"""

RAIN_INI = """; Skyrok - A Weather Mod: heavy rain settings
; Multipliers applied to the rain of every rainy weather, including the ones added by other mods.
; 1.0 = unchanged. Changes show the next time it starts raining.
; The v1.92 Heavy Rain plugin set fixed values per record (density x7.5 to x10, drops half as long);
; these are the averaged equivalents.

[General]
Enabled = true

[Rain]
; number of drops
Density = 8.0
; fall speed
Speed = 1.2
Width = 1.15
Length = 0.5
; size of the area around the player where drops are spawned
Area = 1.1
"""

SNOW_INI = """; Skyrok - A Weather Mod: heavy snow settings
; Multipliers applied to the snow of every snowy weather, including the ones added by other mods.
; 1.0 = unchanged. Changes show the next time it starts snowing.
; The v1.92 Heavy Snow plugin set fixed values per record (density x2 to x3.3, slower, slightly bigger flakes);
; these are the averaged equivalents.

[General]
Enabled = true

[Snow]
; number of flakes
Density = 2.5
; fall and spin speed
Speed = 0.8
Size = 1.15
; size of the area around the player where flakes are spawned
Area = 1.3
"""

here = Path(__file__).parent
for folder, file_name, content in (('HeavyRain', 'Skyrok_Rain.ini', RAIN_INI), ('HeavySnow', 'Skyrok_Snow.ini', SNOW_INI)):
    target = here / folder / file_name
    target.parent.mkdir(exist_ok=True)
    target.write_text(content, encoding='utf-8', newline='\r\n')
    print('wrote', target)
for name, values in IMAGE_SPACE_PRESETS.items():
    target = here / name / 'Skyrok_ImageSpace.ini'
    target.parent.mkdir(exist_ok=True)
    target.write_text(IMAGE_SPACE_TEMPLATE.format(name=name.replace('ImageSpace', ''), exclude=IMAGE_SPACE_EXCLUDE,
                                                  **values), encoding='utf-8', newline='\r\n')
    print('wrote', target)
for name, values in PRESETS.items():
    target = here / name / 'Skyrok_Nights.ini'
    target.parent.mkdir(exist_ok=True)
    target.write_text(TEMPLATE.format(name=name, exclude=EXCLUDE, **values), encoding='utf-8', newline='\r\n')
    print('wrote', target)
for name, values in INTERIOR_PRESETS.items():
    target = here / name / 'Skyrok_Interiors.ini'
    target.parent.mkdir(exist_ok=True)
    target.write_text(INTERIOR_TEMPLATE.format(name=name.replace('Interiors', ''), exclude=INTERIOR_EXCLUDE, **values),
                      encoding='utf-8', newline='\r\n')
    print('wrote', target)

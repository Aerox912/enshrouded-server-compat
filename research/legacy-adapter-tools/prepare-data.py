"""Prepare server-only source changes in an isolated EMM staging directory."""
from pathlib import Path
import sys

stage = Path(sys.argv[1]).resolve()
mods = stage / 'mods'
ember = mods / 'Ember/src/User_Config_Overrides.lua'
text = ember.read_text(encoding='utf-8-sig')
text = text.replace('-- Soulrend: only magic furniture, magic production stations, and buff refresh.\n-- All other feature switches remain disabled.',
                    '-- Second server 1650d1ae: magic storage, buff refresh, no-build zones and Shroud placement.')
for key in ('Enable_BuildingTweaks', 'Enable_PlacementTweaks'):
    text = text.replace(key + ' = false', key + ' = true')
if 'PlacementTweaks_BuildInFog =' not in text:
    text += '\nPlacementTweaks_BuildInFog = true\nPlacementTweaks_NoBuildZoneNeeded = false\n'
ember.write_text(text, encoding='utf-8')

rested = mods / 'XHL-Rested-Enhanced/src/mod.lua'
text = rested.read_text(encoding='utf-8-sig')
needle = '    if #resources ~= 1 then'
addition = '''    -- Dedicated resources deliberately omit client localization. Gameplay is
    -- patched above; matching clients supply the translated tooltip.
    if loader.is_server and #resources == 0 then
        print("[XHL-Rested-Enhanced] Dedicated server: no localization resource; gameplay patch retained.")
        return
    end
'''
if addition not in text:
    assert text.count(needle) == 1, 'Unexpected Rested localization layout'
    text = text.replace(needle, addition + needle)
rested.write_text(text, encoding='utf-8')
print('Prepared second-server EMBER options and dedicated Rested localization guard.')

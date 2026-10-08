"""Verify saved resource readback and assemble the bounded server payload."""
from pathlib import Path
import hashlib,json,re,shutil,zipfile

root=Path(r'E:\Scratch\enshrouded-vein-mining-20261005')
source=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rows(variant,name):
    return {r[0]:r[1:] for line in (root/f'audit-{variant}/export/{name}.tsv').read_text().splitlines() if (r:=line.split('\t'))}
a,b=rows('before','items'),rows('after','items')
assert a.keys()==b.keys() and len(a)==3609
stacks=fog=inhibit=pickaxes=grapples=0
for key,x in a.items():
    y=b[key];assert x[0]==y[0] and x[4]==y[4], 'Identity or altar-zone requirement changed'
    stack=int(x[1]);assert int(y[1])==(65535 if stack>1 else stack)
    stacks+=stack>1
    if x[2]!=y[2]:assert y[2]=='true';fog+=1
    if x[3]!=y[3]:assert x[3] in ('Strict','Lenient') and y[3]=='None';inhibit+=1
    if x[5]!=y[5]:assert float(y[5])==10;pickaxes+=1
    if x[6]!=y[6]:
        old=dict(p.split('=') for p in x[6].split(','));new=dict(p.split('=') for p in y[6].split(','))
        assert float(new['4151084092'])==2*float(old['4151084092'])
        assert new['3078764024']==old['3078764024'];grapples+=1
assert (stacks,fog,inhibit,pickaxes,grapples)==(1901,3283,2764,7,3)
sa,sb=rows('before','scenes'),rows('after','scenes');assert sa.keys()==sb.keys()
assert list(sa.values())==[['9743d1a0-007d-4a8a-9088-82d34ce7a697','283']]
assert list(sb.values())==[['9743d1a0-007d-4a8a-9088-82d34ce7a697','0']]
ba,bb=rows('before','buffs'),rows('after','buffs');assert ba.keys()==bb.keys()
changes=[k for k in ba if ba[k]!=bb[k]]
assert len(changes)==14 and all(ba[k]==['ReplaceAtEnd'] and bb[k]==['Replace'] for k in changes)
ta,tb=rows('before','templates'),rows('after','templates');assert ta.keys()==tb.keys()
magic=[k for k in ta if 'keen::ecs::InventoryCraftingStock' not in ta[k][1] and 'keen::ecs::InventoryCraftingStock' in tb[k][1]]
assert len(magic)==37
pickup=[k for k in ta if 'keen::ecs::PickupItemZone' not in ta[k][1] and 'keen::ecs::PickupItemZone' in tb[k][1]]
assert len(pickup)==93
patch=root/'patch'
flags=dict(re.findall(r'^(Enable_\w+)\s*=\s*(true|false)',(patch/'mods/Ember/src/User_Config_Overrides.lua').read_text(),re.M))
assert {k for k,v in flags.items() if v=='true'}=={'Enable_BuildingTweaks','Enable_PlacementTweaks','Enable_MagicFurniture','Enable_MagicFactories','Enable_BuffReapplication'}
assert digest(patch/'enshrouded_server.exe')=='001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637'
original=Path(r'E:\Scratch\soulrend-ember-20261003\original\enshrouded_server.kfc_resources').read_bytes()
assert (patch/'enshrouded_server.kfc_resources').read_bytes().startswith(original)
report={'items':len(a),'stacks_65535':stacks,'fog_placement_changes':fog,'inhibit_changes':inhibit,
        'removed_no_build_zones':283,'pickaxes_10m':pickaxes,'grapples_2x_pull_unchanged_swing':grapples,
        'buff_refresh':14,'magic_storage_templates':37,'auto_pickup_templates':93,
        'altar_zone_requirements_unchanged':True,'original_resource_prefix_preserved':True,
        'native_focused_checks':64,'multiplayer_gameplay_verified':False}
(root/'validation.json').write_text(json.dumps(report,indent=2))
payload=root/'payload';payload.mkdir(exist_ok=True)
shutil.copytree(patch/'mods',payload/'mods',dirs_exist_ok=True)
for name in ('enshrouded_server.kfc','enshrouded_server.kfc_resources'):shutil.copy2(patch/name,payload/name)
shutil.copy2(r'E:\Build\enshrouded-vein-mining-server\Release\dbghelp.dll',payload/'dbghelp.dll')
shutil.copy2(r'E:\Scratch\enshrouded-server2-20261005\dbghelp.dll',payload/'GlobalXPShare.original.dll')
files=sorted(p for p in payload.rglob('*') if p.is_file())
manifest=[{'file':p.relative_to(payload).as_posix(),'sha256':digest(p),'size':p.stat().st_size} for p in files]
(root/'payload-manifest.json').write_text(json.dumps(manifest,indent=2))
archive=root/'server2-xhl-compatibility-0.1.0.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in files:z.write(p,p.relative_to(payload).as_posix())
print(json.dumps({'validation':report,'files':len(files),'archive':str(archive),'bytes':archive.stat().st_size,'sha256':digest(archive)},indent=2))

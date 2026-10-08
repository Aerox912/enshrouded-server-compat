-- Read-only export of the fields touched by this package. Run without patching.
local function field(o,k)
    if o == nil then return nil end
    local ok,v=pcall(function() return o[k] end)
    if ok then return v end
end
local function write(name,lines)
    table.sort(lines)
    io.export(name,table.concat(lines,'\n'))
end
local items,scenes,buffs,templates,impacts={},{},{},{},{}
for _,r in ipairs(game.assets.get_resources_by_type('keen::ItemInfo')) do
    local d=r.data
    local e=field(d,'equipment')
    local values={}
    local iv=field(d,'impactValues')
    for _,v in ipairs(field(iv,'simple') or {}) do
        local inner=field(v,'value')
        if field(inner,'configId')~=nil then v=inner end
        local raw=field(v,'configId')
        local id=tonumber(raw) or tonumber(field(raw,'value'))
        if id==4151084092 or id==3078764024 then
            local value=field(v,'value')
            values[#values+1]=string.format('%.0f=%s',id,tostring(tonumber(value) or tonumber(field(value,'value'))))
        end
    end
    table.sort(values)
    items[#items+1]=table.concat({tostring(r.guid),tostring(d.debugName),tostring(d.maxStackSize),
        tostring(field(e,'allowPlacementBelowFog')),tostring(field(e,'checkInhibitBuild')),
        tostring(field(e,'buildZoneRequired')),tostring(field(e,'maxDistance')),table.concat(values,',')},'\t')
end
for _,r in ipairs(game.assets.get_resources_by_type('keen::SceneResource')) do
    local zones=field(r.data,'noBuildZones')
    scenes[#scenes+1]=tostring(r.guid)..'\t'..tostring(r.data.ibl)..'\t'..tostring(zones and #zones or -1)
end
for _,r in ipairs(game.assets.get_resources_by_type('keen::BuffType')) do
    buffs[#buffs+1]=tostring(r.guid)..'\t'..tostring(r.data.applyType)
end
for _,r in ipairs(game.assets.get_resources_by_type('keen::ecs::TemplateResource')) do
    local types={}
    for _,c in ipairs(r.data.components or {}) do types[#types+1]=tostring(c.type or c['$type']) end
    templates[#templates+1]=tostring(r.guid)..'\t'..tostring(r.data.name)..'\t'..table.concat(types,',')
end
write('items.tsv',items);write('scenes.tsv',scenes);write('buffs.tsv',buffs);write('templates.tsv',templates)
print('AUDIT items='..#items..' scenes='..#scenes..' buffs='..#buffs..' templates='..#templates)

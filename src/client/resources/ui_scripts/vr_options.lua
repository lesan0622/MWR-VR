-- MWR VR: Options > VR. Edits the same saved dvars as the launcher.
local height_dvar = "vr_playerHeight"
local height_min, height_max = 120, 220

local function is_korean()
    local ok, language = pcall(Engine.GetCurrentLanguage)
    return ok and language == 15 -- LANGUAGE_KOREAN
end

local text = is_korean() and {
    title = "VR",
    button = "VR",
    button_desc = "VR 설정 (키)",
    height = "키",
    height_desc = "허리·등 무기 보관 위치, 가슴 장비 위치, 실제 앉기·엎드리기 판정 거리를 키에 맞춥니다. 좌우로 1cm씩 조절합니다.",
    measure = "지금 키 측정",
    measure_desc = "똑바로 선 채 선택하세요. 헤드셋 높이로 키를 잽니다. 메뉴 밖에서 X 버튼을 3초 눌러도 측정됩니다."
} or {
    title = "VR",
    button = "VR",
    button_desc = "VR settings (height)",
    height = "Height",
    height_desc = "Places the waist and back weapon slots, chest equipment and real crouch/prone distances for your height. Left/right changes it by 1 cm.",
    measure = "Measure height now",
    measure_desc = "Select while standing upright; the headset height sets your height. Holding X for 3 seconds outside menus also measures it."
}

local function height()
    return math.floor(Engine.GetDvarFloat(height_dvar) + 0.5)
end

local function change_height(delta)
    Engine.SetDvarFloat(height_dvar, math.max(height_min, math.min(height_max, height() + delta)))
end

LUI.MenuBuilder.registerType("vr_options_menu", function(root)
    local menu = LUI.MenuTemplate.new(root, {
        menu_title = text.title,
        uppercase_title = true
    })

    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, text.height, text.height_desc,
        function()
            return height() .. " cm"
        end, function()
            change_height(-1)
        end, function()
            change_height(1)
        end)

    menu:AddButton(text.measure, function()
        -- Measures in place; rebuild the menu so the height row shows the result.
        local before = height()
        Engine.ExecNow("vr_measure_height")
        if height() ~= before then
            LUI.FlowManager.RequestLeaveMenu(menu)
            LUI.FlowManager.RequestAddMenu(nil, "vr_options_menu")
        end
    end, nil, true, nil, {
        desc_text = text.measure_desc
    })

    LUI.Options.InitScrollingList(menu.list, nil)
    LUI.Options.AddOptionTextInfo(menu)
    menu:AddBackButton()
    return menu
end)

LUI.addmenubutton("pc_controls", {
    text = text.button,
    description = text.button_desc,
    callback = function()
        LUI.FlowManager.RequestAddMenu(nil, "vr_options_menu")
    end
})

print("[VR options] loaded; korean=" .. tostring(is_korean()) .. " pc_controls=" ..
    tostring(LUI.MenuBuilder.m_types_build["pc_controls"] ~= nil))

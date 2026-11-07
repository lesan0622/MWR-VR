-- MWR VR: Options > VR. Every VR setting this build registers, grouped like
-- the launcher (game:vrsettings reads the launcher schema and locales).
-- Values are the same saved dvars the launcher edits.
local language = 0
pcall(function()
    language = Engine.GetCurrentLanguage()
end)

local function t(key)
    return game:vrsettingtext(language, key)
end

local korean = language == 15 -- LANGUAGE_KOREAN
local extra = korean and {
    button_desc = "VR 설정을 게임 안에서 바꿉니다.",
    measure = "지금 키 측정",
    measure_desc = "똑바로 선 채 선택하세요. 헤드셋 높이로 키를 잽니다. 메뉴 밖에서 X 버튼을 3초 눌러도 측정됩니다."
} or {
    button_desc = "Change VR settings in game.",
    measure = "Measure height now",
    measure_desc = "Select while standing upright; the headset height sets your height. Holding X for 3 seconds outside menus also measures it."
}

local settings = game:vrsettings(language)
local current_category = nil

local function get(name)
    return game:vrsettingget(name)
end

local function visible(entry)
    return entry.whenName == "" or get(entry.whenName) == entry.whenValue
end

local function controls_visibility(name)
    for i = 1, #settings do
        if settings[i].whenName == name then
            return true
        end
    end
    return false
end

local function decimals(entry)
    local step = entry.step / entry.scale
    local digits = 0
    while step < 0.999 and digits < 3 do
        step = step * 10
        digits = digits + 1
    end
    return digits
end

local function number_text(entry)
    local value = (tonumber(get(entry.name)) or 0) / entry.scale
    return string.format("%." .. decimals(entry) .. "f", value)
end

local function reopen(menu)
    LUI.FlowManager.RequestLeaveMenu(menu)
    LUI.FlowManager.RequestAddMenu(nil, "vr_options_category")
end

local function changed(menu, entry)
    if controls_visibility(entry.name) then
        reopen(menu)
    end
end

local function add_toggle(menu, entry)
    local function on()
        local value = get(entry.name)
        return value ~= "" and value ~= "0"
    end
    local function flip()
        game:vrsettingset(entry.name, on() and "0" or tostring(entry.enabledLevel))
        changed(menu, entry)
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return t(on() and "choice.enabled" or "choice.disabled")
        end, flip, flip)
end

local function add_choice(menu, entry)
    local function index()
        local value = get(entry.name)
        for i = 1, #entry.options do
            if entry.options[i].value == value then
                return i
            end
        end
        return 1
    end
    local function step(delta)
        local count = #entry.options
        local next_index = ((index() - 1 + delta) % count) + 1
        game:vrsettingset(entry.name, entry.options[next_index].value)
        changed(menu, entry)
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return entry.options[index()].label
        end, function()
            step(-1)
        end, function()
            step(1)
        end)
end

local function add_number(menu, entry)
    local function step(delta)
        local value = tonumber(get(entry.name)) or entry.min
        local steps = math.floor((value - entry.min) / entry.step + 0.5) + delta
        local next_value = math.max(entry.min, math.min(entry.max, entry.min + steps * entry.step))
        game:vrsettingset(entry.name, string.format("%.6g", next_value))
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return number_text(entry)
        end, function()
            step(-1)
        end, function()
            step(1)
        end)
end

local function add_divider(menu, label)
    if not LUI.MenuBuilder.m_types_build["h1_option_menu_titlebar"] or label == "" then
        return
    end
    local element = LUI.UIElement.new({
        leftAnchor = true,
        rightAnchor = true,
        left = 0,
        right = 0,
        topAnchor = true,
        bottomAnchor = false,
        top = 0,
        bottom = 33.33
    })
    element.scrollingToNext = true
    element:addElement(LUI.MenuBuilder.BuildRegisteredType("h1_option_menu_titlebar", {
        title_bar_text = label
    }))
    menu.list:addElement(element)
end

local function add_measure(menu)
    menu:AddButton(extra.measure, function()
        -- Measures in place; rebuild the menu so the height row shows the result.
        local before = get("vr_playerHeight")
        Engine.ExecNow("vr_measure_height")
        if get("vr_playerHeight") ~= before then
            reopen(menu)
        end
    end, nil, true, nil, {
        desc_text = extra.measure_desc
    })
end

LUI.MenuBuilder.registerType("vr_options_category", function(root)
    local title = "VR"
    for i = 1, #settings do
        if settings[i].category == current_category then
            title = settings[i].categoryLabel
            break
        end
    end
    local menu = LUI.MenuTemplate.new(root, {
        menu_title = title,
        uppercase_title = true,
        menu_top_indent = LUI.MenuTemplate.spMenuOffset,
        menu_width = GenericMenuDims.OptionMenuWidth
    })
    local group = nil
    for i = 1, #settings do
        local entry = settings[i]
        if entry.category == current_category and visible(entry) then
            if entry.group ~= group then
                group = entry.group
                add_divider(menu, entry.groupLabel)
            end
            if entry.type == "toggle" then
                add_toggle(menu, entry)
            elseif entry.type == "choice" then
                add_choice(menu, entry)
            else
                add_number(menu, entry)
            end
            if entry.name == "vr_playerHeight" then
                add_measure(menu)
            end
        end
    end
    LUI.Options.InitScrollingList(menu.list, nil, {
        rows = 11
    })
    LUI.Options.AddOptionTextInfo(menu)
    menu:AddBackButton()
    return menu
end)

LUI.MenuBuilder.registerType("vr_options_menu", function(root)
    local menu = LUI.MenuTemplate.new(root, {
        menu_title = "VR",
        uppercase_title = true
    })
    local seen = {}
    for i = 1, #settings do
        local category = settings[i].category
        if not seen[category] then
            seen[category] = true
            menu:AddButton(settings[i].categoryLabel, function()
                current_category = category
                LUI.FlowManager.RequestAddMenu(nil, "vr_options_category")
            end, nil, true, nil, {
                desc_text = extra.button_desc
            })
        end
    end
    LUI.Options.InitScrollingList(menu.list, nil)
    LUI.Options.AddOptionTextInfo(menu)
    menu:AddBackButton()
    return menu
end)

LUI.addmenubutton("pc_controls", {
    text = "VR",
    description = extra.button_desc,
    callback = function()
        LUI.FlowManager.RequestAddMenu(nil, "vr_options_menu")
    end
})

print("[VR options] loaded; settings=" .. #settings .. " language=" .. tostring(language) .. " titlebar=" ..
    tostring(LUI.MenuBuilder.m_types_build["h1_option_menu_titlebar"] ~= nil))

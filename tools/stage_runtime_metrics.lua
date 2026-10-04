-- Appended to a local game probe by stage_runtime_validation.py.
-- Reads per-build linker symbols; writes only emulator counters and captures.
do
    local symbols = {}
    local map = assert(io.open(os.getenv("MK2_ROOT") .. "/src/MK2.MAP", "r"))
    for line in map:lines() do
        for address, name in line:gmatch("(%x%x%x%x%x%x%x%x)%s+([%w_%.]+)") do
            symbols[name] = tonumber(address, 16)
        end
    end
    map:close()
    local pressure = {peak_overload=0, qdma_drops=0, deep_dmaq=0, dmaq_over=0, dmaq_late=0, dmaq_lost=0,
                      palq_drops=0, fpal_fail=0}
    for name in pairs(pressure) do
        assert(symbols[name], "Missing runtime pressure symbol " .. name)
    end
    for _, name in ipairs({"gstate", "curback", "ofree", "f_death"}) do
        assert(symbols[name], "Missing runtime metric symbol " .. name)
    end
    local expected = tonumber(os.getenv("VALIDATION_STAGE"))
    local play = os.getenv("VALIDATION_PLAY") == "1"
    local camera_min, camera_max, hp1, hp2, wrong_matchup = 32767, -32768, 161, 161, 0
    if play then
        for _, name in ipairs({"worldtlx", "p1_bar", "p2_bar", "p1_char", "p2_char"}) do
            assert(symbols[name], "Missing natural-play symbol " .. name)
        end
    end
    local function signed(v) return v >= 32768 and v - 65536 or v end
    local space, frame, samples, minimum, peak, drops = nil, 0, 0, 1000, 0, 0
    local function read_pressure()
        for name, value in pairs(pressure) do
            pressure[name] = math.max(value, space:read_u16(symbols[name]))
        end
        peak, drops = pressure.peak_overload, pressure.qdma_drops
    end
    local wrong, broken, captures = 0, 0, 0
    local play_frames, fight_exits, last_state = 0, 0, nil
    local transition_end, transition_captures = -1, 0
    local select_field
    emu.register_frame(function()
        frame = frame + 1
        if frame == 1 then
            for _, port in pairs(manager.machine.ioport.ports) do
                for _, field in pairs(port.fields) do
                    if field.name == "UI Select" then select_field = field end
                end
            end
        end
        if select_field and frame <= 36 then
            select_field:set_value((frame >= 5 and frame < 35) and 1 or 0)
        end
        space = space or manager.machine.devices[":maincpu"].spaces["program"]
        -- Palette setup can fail before the first fighting frame. Preserve
        -- these counters from boot instead of clearing them at combat start.
        for _, name in ipairs({"palq_drops", "fpal_fail"}) do
            pressure[name] = math.max(pressure[name], space:read_u16(symbols[name]))
        end
        if samples > 0 then read_pressure() end
        local state = space:read_u16(symbols.gstate)
        local dense = false
        if play and (samples > 0 or state == 2) then
            play_frames = play_frames + 1
            if last_state == 2 and state ~= 2 then fight_exits = fight_exits + 1 end
            if last_state ~= state then transition_end = frame + 7 end
            dense = frame <= transition_end
            last_state = state
            -- Keep observing natural round/result transitions after combat ends.
            -- Partial redraws can last only one frame: sample every frame for
            -- the first eight frames of each state, independent of periodic shots.
            if state ~= 2 and (dense or play_frames % 30 == 0) then
                manager.machine.screens[":screen"]:snapshot()
                captures = captures + 1
                if dense then transition_captures = transition_captures + 1 end
                validation_print(string.format(
                    "[validation] SNAP frame=%d sample=%d stage=%d death=%d gstate=%d dense=%d",
                    frame, samples, space:read_u16(symbols.curback), space:read_u16(symbols.f_death), state, dense and 1 or 0))
            end
        end
        if state ~= 2 then return end
        local stage = space:read_u16(symbols.curback)
        if stage ~= expected then wrong = wrong + 1; return end
        if samples == 0 then
            for name in pairs(pressure) do
                if name ~= "palq_drops" and name ~= "fpal_fail" then space:write_u16(symbols[name], 0) end
            end
        end
        samples = samples + 1
        local camera, bar1, bar2
        if play then
            camera = signed(space:read_u32(symbols.worldtlx) >> 16)
            bar1, bar2 = space:read_u16(symbols.p1_bar), space:read_u16(symbols.p2_bar)
            camera_min, camera_max = math.min(camera_min, camera), math.max(camera_max, camera)
            hp1, hp2 = math.min(hp1, bar1), math.min(hp2, bar2)
            if space:read_u16(symbols.p1_char) ~= tonumber(os.getenv("SWEEP_P1")) or
               space:read_u16(symbols.p2_char) ~= tonumber(os.getenv("SWEEP_P2")) then
                wrong_matchup = wrong_matchup + 1
            end
        end
        local obj, count, seen = space:read_u32(symbols.ofree), 0, {}
        while obj ~= 0 and count < 1000 and not seen[obj] do
            seen[obj] = true
            count = count + 1
            obj = space:read_u32(obj)
        end
        if obj ~= 0 then broken = broken + 1 end
        minimum = math.min(minimum, count)
        read_pressure()
        local death = space:read_u16(symbols.f_death)
        local interval = (death > 0 and death < 0x8000) and 5 or 30
        if dense or samples % interval == 0 then
            manager.machine.screens[":screen"]:snapshot()
            captures = captures + 1
            if dense then transition_captures = transition_captures + 1 end
            local detail = play and string.format(" camera=%d hp1=%d hp2=%d gstate=%d dense=%d", camera, bar1, bar2, state, dense and 1 or 0) or ""
            validation_print(string.format("[validation] SNAP frame=%d sample=%d stage=%d death=%d",
                frame, samples, stage, death) .. detail)
        end
    end)
    emu.register_stop(function()
        if space and samples > 0 then read_pressure() end
        validation_print(string.format(
            "[validation] METRICS samples=%d min_free=%d peak=%d drops=%d wrong_stage=%d broken_list=%d captures=%d queue_words=%d queue_overflows=%d late_frames=%d lost_entries=%d palette_drops=%d palette_failures=%d",
            samples, minimum, peak, drops, wrong, broken, captures,
            pressure.deep_dmaq, pressure.dmaq_over, pressure.dmaq_late, pressure.dmaq_lost,
            pressure.palq_drops, pressure.fpal_fail))
        if play then
            validation_print(string.format(
                "[validation] PLAY samples=%d frames=%d fight_exits=%d camera_min=%d camera_max=%d p1_min_hp=%d p2_min_hp=%d wrong_matchup=%d transition_captures=%d",
                samples, play_frames, fight_exits, camera_min, camera_max, hp1, hp2, wrong_matchup, transition_captures))
        end
    end)
end

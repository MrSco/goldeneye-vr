-- BizHawk (ares N64 core) Lua: press through a hack's title, folder, mission
-- select (its first mission), difficulty and briefing, then dump the data
-- segment and the Expansion Pak pieces from RDRAM, with a screenshot, at the
-- frames asked for. For tools/emu/compare_window.py against the PC build's
-- "windowdump" (port/pc/pc_testinput.c). No ROM data is kept.
--
--   set GEVR_EMU_OUT=<folder>/   GEVR_EMU_DUMPS=1600:dam_a,2000:dam_b   [GEVR_EMU_PRESS_UNTIL=1150]
--   EmuHawk.exe --lua=tools/emu/n64_dump.lua <hack>.z64
--
-- EmuHawk's config.ini: PreferredCores N64 = Ares64 (Mupen64Plus gave no RDRAM
-- domain and noise on screen, 2026-11-17).
local out = os.getenv("GEVR_EMU_OUT") or "./"
local press_until = tonumber(os.getenv("GEVR_EMU_PRESS_UNTIL") or "1150")
local dumps, last = {}, 0
for frame, name in string.gmatch(os.getenv("GEVR_EMU_DUMPS") or "1600:dump", "(%d+):([%w_]+)") do
  dumps[tonumber(frame)] = name
  last = math.max(last, tonumber(frame))
end
local ranges = {
  { 0x00020D90, 0x0003C550 },   -- the data segment at 0x80020D90 (Mario Characters' length)
  { 0x00700000, 0x00012800 },   -- 0x80700000-0x80712800: the hack's own variables and Expansion Pak pieces
}

local function dump(name)
  local f = io.open(out .. name .. ".bin", "wb")
  for _, r in ipairs(ranges) do
    local at, left = r[1], r[2]
    while left > 0 do
      local n = math.min(left, 4096)
      local bytes = memory.read_bytes_as_array(at, n, "RDRAM")
      local parts = {}
      for k = 1, n do parts[k] = string.char(bytes[k]) end
      f:write(table.concat(parts))
      at, left = at + n, left - n
    end
  end
  f:close()
end

for i = 1, last do
  if i < press_until and (i % 45) < 4 then
    joypad.set({ ["P1 Start"] = (i % 90) < 4, ["P1 A"] = (i % 90) >= 45 })
  end
  emu.frameadvance()
  if dumps[i] then
    dump(dumps[i])
    client.screenshot(out .. dumps[i] .. ".png")
  end
end
client.exit()

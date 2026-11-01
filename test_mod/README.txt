Engine test mod (not for play)

Console checks for engine features, kept out of the shipped mods. Copy this folder into MO2\mods
as a mod of its own (e.g. "Engine Test") when testing, and disable or remove it afterwards: the
DLTX file gives three vanilla addons handling keys they don't have in the game.

Every command is run_string <script>.<name>() with a gun in hand (lowercase: the console
lowercases what you type); the values go to the log. Gunsmith Docs 04 has the checklists, doc 24
the features.

  gamedata\scripts\hud_offset_test.script        per-object HUD offsets (game_object:set_hud_offset)
  gamedata\scripts\hud_ik_test.script            hand IK targets (get_player_hud():set_ik_target); number pad keys
  gamedata\scripts\gs_vanilla_readings_test.script  vanilla readings of the addon multipliers, weight and cost
  gamedata\configs\mod_system_gs_vanilla_readings_test.ltx
                                                 the SIG 550's suppressor and M203 and the K98's ZF4 get
                                                 the four addon keys the readings test reports

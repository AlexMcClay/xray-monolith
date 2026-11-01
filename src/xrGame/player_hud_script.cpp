#include "pch_script.h"
#include "player_hud.h"

using namespace luabind;

player_hud* get_player_hud()
{
	return g_player_hud;
}

#pragma optimize("s",on)
void player_hud::script_register(lua_State* L)
{
	module(L)
	[
		class_<player_hud>("player_hud")
		.def(constructor<>())
		.def("set_hands", &player_hud::load_script)
		.def("reset_hands", &player_hud::reset_model_script)
		// hand IK (hand 0 right, 1 left): the hand bone on a frame in a HUD item's model space or its bone's
		.def("set_ik_target", &player_hud::set_ik_target)
		.def("set_ik_target", &player_hud::set_ik_target_bone)
		.def("clear_ik_target", &player_hud::clear_ik_target)
		.def("set_ik_weight", &player_hud::set_ik_weight)
		.def("get_ik_weight", &player_hud::get_ik_weight)
		.def("get_ik_hand", &player_hud::get_ik_hand)
		.def("get_ik_hand", &player_hud::get_ik_hand_bone),

		def("get_player_hud", &get_player_hud)
	];
}

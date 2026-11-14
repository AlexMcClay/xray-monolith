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
		// the hands (hand 0 right, 1 left): this frame's finger pose, an arm model's transform (hud_hand_ik.script)
		.def("set_ik_fingers", &player_hud::set_ik_fingers)
		.def("get_arm_transform", &player_hud::get_arm_transform)
		.def("item_attached", &player_hud::item_attached)
		.def("get_item_transform", &player_hud::get_item_transform)
		// animation layers: a HUD motion on one bone partition of a HUD item's model, over what the item plays
		.def("play_item_layer", &player_hud::play_item_layer)
		.def("stop_item_layer", &player_hud::stop_item_layer)
		.def("item_layer", &player_hud::item_layer),

		def("get_player_hud", &get_player_hud)
	];
}

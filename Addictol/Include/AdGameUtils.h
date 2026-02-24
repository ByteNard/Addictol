#pragma once

#include <string_view>

namespace RE
{
	class TESObjectREFR;
}

namespace Addictol
{
	using namespace std::literals;

	bool ExecuteCommand(std::string_view a_command, RE::TESObjectREFR* a_targetRef, bool a_silent);
}
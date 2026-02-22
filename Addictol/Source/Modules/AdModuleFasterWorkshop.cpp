#include <Modules/AdModuleFasterWorkshop.h>
#include <AdUtils.h>

#include <RE/K/KeywordType.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormUtil.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesFasterWorkshop{ "Patches"sv, "bFasterWorkshop"sv, true };

	namespace fasterWorkshopDetail
	{
		REL::Relocation<std::uintptr_t> AddLeafNodeHookTarget{ REL::ID{ 934716, 2195247 }, REL::Offset{ 0x1EF, 0x2F } };

		// Constructible Object Map
		std::unordered_map<const RE::BGSKeyword*, std::vector<const RE::BGSConstructibleObject*>> g_cobjMap;

		// ---- Game Functions ---- //

		inline bool WorkshopCanShowRecipe(const RE::BGSConstructibleObject* a_recipe, const RE::BGSKeyword* a_filter)
		{
			using func_t = decltype(&WorkshopCanShowRecipe);
			static REL::Relocation<func_t> func{ REL::ID{ 239190, 2194978 } };
			return func(a_recipe, a_filter);
		}

		inline const RE::BGSKeyword* GetKeywordByIndex(RE::KeywordType a_type, std::uint16_t a_index)
		{
			using func_t = decltype(&GetKeywordByIndex);
			static REL::Relocation<func_t> func{ REL::ID{ 952856, 2206423 } };
			return func(a_type, a_index);
		}

		inline const void TryAddLeafNode(void* unk1, const RE::BGSConstructibleObject* cobj)
		{
			using func_t = decltype(&TryAddLeafNode);
			static REL::Relocation<func_t> func{ REL::ID{ 281929 } };
			return func(unk1, cobj);
		}

		// ---- Map Functions ---- //

		inline bool ClearBuiltMap()
		{
			REX::INFO("FasterWorkshop: Clearing COBJ Map");

			g_cobjMap.clear();
			g_cobjMap.reserve(3000);

			return true;
		}

		inline static auto TryBuildMap() noexcept -> void
		{
			// Only Build if necessary
			if (g_cobjMap.empty())
			{
				REX::INFO("FasterWorkshop: Building Map...");

				// Data Handler
				const auto dataHandler = RE::TESDataHandler::GetSingleton();

				if (!dataHandler)
				{
					REX::INFO("FasterWorkshop: Invalid DataHandler");
					return;
				}

				// Objects Array
				auto objectArray{ dataHandler->GetFormArray<RE::BGSConstructibleObject>() };

				if (objectArray.empty())
				{
					REX::INFO("FasterWorkshop: Empty Objects Array.");
					return;
				}

				REX::INFO("FasterWorkshop: Objects Array Size is {}", objectArray.size());

				if (objectArray.size() > 50000)
				{
					REX::WARN("FasterWorkshop: Warning, Objects Array is larger than 50,000!");
				}

				// Objects Loop
				for (const auto& object : objectArray)
				{
					if (!object || !object->createdItem)
					{
						continue;
					}

					// Use only 1 Keyword from Possibly Corrupt Objects
					uint32_t count = object->filterKeywords.size;
					if (count > 32)
					{
						count = 1;
					}

					// Recipe Filter
					auto recipeFiltered{ std::span{object->filterKeywords.array, count} };

					if (recipeFiltered.empty())
					{
						// Keyword Count is Empty
						continue;
					}

					for (int i{ 0 }; i < recipeFiltered.size(); ++i)
					{
						if ((recipeFiltered.data() + i) == nullptr)
						{
							REX::INFO("FasterWorkshop: recipeFiltered.data() + {} is Null.", i);
							continue;
						}

						auto& keywordValue{ recipeFiltered[i] };
						if (const RE::BGSKeyword* keyword{ GetKeywordByIndex(RE::KeywordType::kRecipeFilter, keywordValue.keywordIndex) }; keyword)
						{
							g_cobjMap[keyword].push_back(object);
						}
					}
				}

				REX::INFO("FasterWorkshop: Map Built. {} Map Key Elements. {} Total COBJ Elements Processed.", g_cobjMap.size(), objectArray.size());

				std::size_t total{};
				for (const auto& [key, value] : g_cobjMap)
				{
					total += value.size();
				}

				REX::INFO("FasterWorkshop: Total COBJ Elements in Map: {}", total);
			}
		}

		// ---- Patch Functions ---- //

		inline static auto HandlerMatches(const RE::BGSConstructibleObject* obj, const RE::BGSKeyword* keyword) noexcept -> bool
		{
			TryBuildMap();

			if (g_cobjMap.contains(keyword))
			{
				for (auto* cobj : g_cobjMap[keyword])
				{
					if (obj == cobj)
					{
						if (WorkshopCanShowRecipe(obj, keyword))
						{
							return true;
						}
					}
				}
			}

			return false;
		}

		inline static void HandlerAddLeafNode(void* unk1, const RE::BGSKeyword* keyword)
		{
			TryBuildMap();

			if (g_cobjMap.contains(keyword))
			{
				for (auto* cobj : g_cobjMap[keyword])
				{
					if (RELEX::IsRuntimeOG())
						TryAddLeafNode(unk1, cobj);
				}
			}
		}

		struct AddLeafNodePatch : Xbyak::CodeGenerator
		{
			AddLeafNodePatch() : Xbyak::CodeGenerator()
			{
				Xbyak::Label continueLabel, handler;

				lea(rcx, ptr[rbp - 0x28]);
				mov(rdx, ptr[rbp - 0x28]);
				mov(rdx, ptr[rdx]);
				call(ptr[rip + handler]);
				jmp(ptr[rip + continueLabel]);

				L(continueLabel);
				if (RELEX::IsRuntimeOG())
					dq(AddLeafNodeHookTarget.address() + 0x23);
				else
					dq(AddLeafNodeHookTarget.address() + 0x148);

				L(handler);
				dq((uintptr_t)&HandlerAddLeafNode);
			}
		};
	}

	ModuleFasterWorkshop::ModuleFasterWorkshop() :
		Module("Faster Workshop", &bPatchesFasterWorkshop, { F4SE::MessagingInterface::kGameDataReady })
	{}

	bool ModuleFasterWorkshop::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleFasterWorkshop::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> HookLeafTarget{ REL::ID{ 281929, 2195247 }, REL::Offset{ 0x4B, 0x93 } };
		REL::Relocation<std::uintptr_t> HookNonLeafTarget{ REL::ID{ 157820, 2195021 }, REL::Offset{ 0x7C, 0xDA } };
		REL::Relocation<std::uintptr_t> HookIconLoadLagTarget{ REL::ID{ 1280212, 2224975 }, REL::Offset{ 0x3A5, 0x3A0 } };

		// Ready the AddLeafNode Patch
		fasterWorkshopDetail::AddLeafNodePatch p{};
		p.ready();

		// Workshop Lag Fix
		auto& trampoline = REL::GetTrampoline();
		if (RELEX::IsRuntimeOG()) trampoline.write_jmp<5>(fasterWorkshopDetail::AddLeafNodeHookTarget.address(), trampoline.allocate(p));
		trampoline.write_call<5>(HookLeafTarget.address(), reinterpret_cast<std::uintptr_t>(fasterWorkshopDetail::HandlerMatches));
		trampoline.write_call<5>(HookNonLeafTarget.address(), reinterpret_cast<std::uintptr_t>(fasterWorkshopDetail::HandlerMatches));

		// Icon Lag Fix
		if (RELEX::IsRuntimeOG())
		{
			std::array Payload{ std::uint8_t{0x90}, std::uint8_t{0x90}, std::uint8_t{0x90} };
			REL::WriteSafe(HookIconLoadLagTarget.address(), Payload.data(), Payload.size());
		}
		else
		{
			std::array Payload{ std::uint8_t{0x66}, std::uint8_t{0x90} };
			REL::WriteSafe(HookIconLoadLagTarget.address(), Payload.data(), Payload.size());
		}

		return true;
	}

	bool ModuleFasterWorkshop::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			fasterWorkshopDetail::ClearBuiltMap();
		}

		return true;
	}

	bool ModuleFasterWorkshop::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
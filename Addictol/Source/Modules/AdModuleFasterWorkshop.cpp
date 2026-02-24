#include <Modules/AdModuleFasterWorkshop.h>
#include <AdUtils.h>

#include <RE/K/KeywordType.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormUtil.h>

#include <RE/B/BGSConstructibleObject.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/B/BGSListForm.h>
#include <RE/W/Workshop.h>

#include <xbyak/xbyak.h>

#define AD_NOMESSAGE_FASTERWORKSHOP 1

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesFasterWorkshop{ "Patches"sv, "bFasterWorkshop"sv, true };

	namespace fasterWorkshopDetail
	{
		REL::Relocation<std::uintptr_t> AddLeafNodeHookTarget{ REL::ID{ 934716, 2195247 }, REL::Offset{ 0x1EF, 0x2F } };

		// Constructible Object Map
		std::unordered_map<const RE::BGSKeyword*, std::vector<const RE::BGSConstructibleObject*>> g_cobjMap;

		// ---- Game Functions ---- //

		inline static bool WorkshopCanShowRecipe(const RE::BGSConstructibleObject* a_recipe, const RE::BGSKeyword* a_filter) noexcept
		{
			using func_t = decltype(&WorkshopCanShowRecipe);
			static REL::Relocation<func_t> func{ REL::ID{ 239190, 2194978 } };
			return func(a_recipe, a_filter);
		}

		inline static const RE::BGSKeyword* GetKeywordByIndex(RE::KeywordType a_type, std::uint16_t a_index) noexcept
		{
			using func_t = decltype(&GetKeywordByIndex);
			static REL::Relocation<func_t> func{ REL::ID{ 952856, 2206423 } };
			return func(a_type, a_index);
		}

		inline static void TryAddLeafNode(void* unk1, const RE::BGSConstructibleObject* cobj) noexcept
		{
			using func_t = decltype(&TryAddLeafNode);
			static REL::Relocation<func_t> func{ REL::ID{ 281929 } };
			return func(unk1, cobj);
		}

		// ---- Map Functions ---- //

		inline static bool ClearBuiltMap() noexcept
		{
#if !AD_NOMESSAGE_FASTERWORKSHOP
			REX::INFO("FasterWorkshop: Clearing COBJ Map");
#endif

			g_cobjMap.clear();
			g_cobjMap.reserve(3000);

			return true;
		}

		inline static auto TryBuildMap() noexcept -> void
		{
			// Only Build if necessary
			if (g_cobjMap.empty())
			{
#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Building Map...");
#endif

				// Data Handler
				const auto dataHandler = RE::TESDataHandler::GetSingleton();

				if (!dataHandler)
				{
#if !AD_NOMESSAGE_FASTERWORKSHOP
					REX::INFO("FasterWorkshop: Invalid DataHandler");
#endif
					return;
				}

				// Objects Array
				auto& objectArray = dataHandler->GetFormArray<RE::BGSConstructibleObject>();

				if (objectArray.empty())
				{
#if !AD_NOMESSAGE_FASTERWORKSHOP
					REX::INFO("FasterWorkshop: Empty Objects Array.");
#endif
					return;
				}

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Objects Array Size is {}", objectArray.size());
#endif

				if (objectArray.size() > 50000)
					REX::WARN("FasterWorkshop: Warning, Objects Array is larger than 50,000!");

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
#if !AD_NOMESSAGE_FASTERWORKSHOP
							REX::INFO("FasterWorkshop: recipeFiltered.data() + {} is Null.", i);
#endif
							continue;
						}

						auto& keywordValue{ recipeFiltered[i] };
						if (const RE::BGSKeyword* keyword{ GetKeywordByIndex(RE::KeywordType::kRecipeFilter, keywordValue.keywordIndex) }; keyword)
						{
							g_cobjMap[keyword].push_back(object);
						}
					}
				}

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Map Built. {} Map Key Elements. {} Total COBJ Elements Processed.", g_cobjMap.size(), objectArray.size());
#endif

				std::size_t total{};
				for (const auto& [key, value] : g_cobjMap)
				{
					total += value.size();
				}

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Total COBJ Elements in Map: {}", total);
#endif
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

	//struct TBullshit_v2
	//{
	//	int64_t a[2];
	//};

	//struct TBullshit_v1
	//{
	//	RE::BGSKeyword** workshopRecipeFilter;
	//	bool a1;
	//	TBullshit_v2 bullshit;
	//};

	//static void AddSomethingBullshit(TBullshit_v1* a_bullshit, RE::BGSConstructibleObject** a_conjObject) noexcept
	//{
	//	auto conjObject = *a_conjObject;
	//	auto filter = *(a_bullshit->workshopRecipeFilter);

	//	if (!conjObject || !filter || filter->IsDeleted())
	//		return;

	//	// for OG 0x17E, but AE 0x17F, clib wrong
	//	auto defaultConjObject = GetDefaultObjectFromDefaultManager(0x17F);
	//	if (RE::Workshop::WorkshopCanShowRecipe(conjObject, (defaultConjObject == filter ? nullptr : filter)))
	//	{
	//		a_bullshit->a1 = true;
	//		TBullshit_v2 bullshit_v2{ a_bullshit->bullshit };
	//		auto createdItem = conjObject->createdItem;
	//		if (createdItem && createdItem->formType == RE::ENUM_FORM_ID::kFLST)
	//		{
	//			auto listForm = reinterpret_cast<RE::BGSListForm*>(createdItem);
	//			if (!listForm->arrayOfForms.empty())
	//			{
	//				for (auto form : listForm->arrayOfForms)
	//				{
	//					if (form->formType != RE::ENUM_FORM_ID::kKYWD)
	//					{
	//						//REX::INFO("{}", form->GetFormID());
	//						AddSomethingBullshitToMenu(bullshit_v2, conjObject, nullptr, conjObject, form);
	//					}
	//					else
	//						AddSomethingBullshitToMenu(bullshit_v2, conjObject, form, conjObject, nullptr);
	//				}
	//			}
	//		}
	//		else
	//			AddSomethingBullshitToMenu(bullshit_v2, conjObject, nullptr, nullptr, createdItem);
	//	}
	//}

	struct Workshop__Workbench__Node
	{
		int64_t* a1;
		int64_t* a2;
	};

	struct Workshop__Workbench__ListNode
	{
		Workshop__Workbench__Node* a1;
		const RE::BGSConstructibleObject* recipe;
	};

	struct Workshop__Workbench__FilterNode
	{
		RE::BGSKeyword* keyword;
	};

	struct Workshop__Workbench__MenuNode
	{
		Workshop__Workbench__FilterNode* filterNode;
		bool* a2;	// initialized ???
		Workshop__Workbench__Node node;
	};

	using TWorkshop__Workbench__AddRecipe = void (*)(Workshop__Workbench__Node*, const RE::BGSConstructibleObject*,
		RE::TESForm*, const RE::BGSConstructibleObject*, RE::TESForm*);
	TWorkshop__Workbench__AddRecipe Workshop__Workbench__AddRecipe;

	using TGetDefaultObjectFromDefaultManager = RE::TESForm* (*)(std::uint32_t);
	TGetDefaultObjectFromDefaultManager GetDefaultObjectFromDefaultManager;

	static void Workshop__Workbench__ForEachInListAddRecipe(Workshop__Workbench__ListNode& a_node, const RE::BGSListForm* a_formList) noexcept
	{
		auto createdRecipe = a_node.recipe;
		for (auto createdItem : a_formList->arrayOfForms)
		{
			if (createdItem->GetFormType() != RE::ENUM_FORM_ID::kKYWD)
				Workshop__Workbench__AddRecipe(a_node.a1, createdRecipe, nullptr, createdRecipe, createdItem);
			else
				Workshop__Workbench__AddRecipe(a_node.a1, createdRecipe, createdItem, createdRecipe, nullptr);
		}
	}

	static void Workshop__Workbench__TryAddRecipe(Workshop__Workbench__MenuNode* a_menuNode, const RE::BGSConstructibleObject* a_formRecipe) noexcept
	{
		auto filter = a_menuNode->filterNode->keyword;
		if (!a_formRecipe || !filter || filter->IsDeleted())
			return;

		// for OG 0x17E, but AE 0x17F, clib wrong
		auto defaultObject = GetDefaultObjectFromDefaultManager(0x17F);
		if (RE::Workshop::WorkshopCanShowRecipe(const_cast<RE::BGSConstructibleObject*>(a_formRecipe),
			(defaultObject == filter ? nullptr : filter)))
		{
			*a_menuNode->a2 = true;
			Workshop__Workbench__Node node{ a_menuNode->node };
			auto createdItem = a_formRecipe->createdItem;
			if (createdItem && createdItem->formType == RE::ENUM_FORM_ID::kFLST)
			{
				Workshop__Workbench__ListNode listNode{ &node, a_formRecipe };
				Workshop__Workbench__ForEachInListAddRecipe(listNode, reinterpret_cast<RE::BGSListForm*>(createdItem));
			}
			else
				Workshop__Workbench__AddRecipe(&node, a_formRecipe, nullptr, nullptr, createdItem);
		}
	}

	// a_typeForm == always RE::ENUM_FORM_ID::kCOBJ
	static void Workshop__Workbench__StoreAll(RE::TESDataHandler* a_dataHandler, 
		[[maybe_unused]] const REX::EnumSet<RE::ENUM_FORM_ID, uint8_t> a_typeForm, Workshop__Workbench__MenuNode* a_menuNode) noexcept
	{
		auto& arrRecipeForm = a_dataHandler->GetFormArray<RE::BGSConstructibleObject>();
		for (auto formRecipe : arrRecipeForm)
			Workshop__Workbench__TryAddRecipe(a_menuNode, formRecipe);
	}

	ModuleFasterWorkshop::ModuleFasterWorkshop() :
		Module("Faster Workshop", &bPatchesFasterWorkshop, { F4SE::MessagingInterface::kGameDataReady })
	{}

	bool ModuleFasterWorkshop::DoQuery() const noexcept
	{
		return RELEX::IsRuntimeAE();
		//return true;
	}

	bool ModuleFasterWorkshop::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		//RE::Workshop::WorkshopMenuNode

		//REL::Relocation<std::uintptr_t> HookLeafTarget{ REL::ID{ 281929, 2195247 }, REL::Offset{ 0x4B, 0x93 } };
		//REL::Relocation<std::uintptr_t> HookNonLeafTarget{ REL::ID{ 157820, 2195021 }, REL::Offset{ 0x7C, 0xDA } };
		//REL::Relocation<std::uintptr_t> HookIconLoadLagTarget{ REL::ID{ 1280212, 2224975 }, REL::Offset{ 0x3A5, 0x3A0 } };

		//// Ready the AddLeafNode Patch
		//fasterWorkshopDetail::AddLeafNodePatch p{};
		//p.ready();

		//// Workshop Lag Fix
		//auto& trampoline = REL::GetTrampoline();
		//if (RELEX::IsRuntimeOG()) trampoline.write_jmp<5>(fasterWorkshopDetail::AddLeafNodeHookTarget.address(), trampoline.allocate(p));
		//trampoline.write_call<5>(HookLeafTarget.address(), reinterpret_cast<std::uintptr_t>(fasterWorkshopDetail::HandlerMatches));
		//trampoline.write_call<5>(HookNonLeafTarget.address(), reinterpret_cast<std::uintptr_t>(fasterWorkshopDetail::HandlerMatches));

		// Pretty nice bullshit code install
		*(uintptr_t*)&Workshop__Workbench__AddRecipe = REL::ID(2195494).address();
		*(uintptr_t*)&GetDefaultObjectFromDefaultManager = REL::ID(2192850).address();

		/*auto offset = REL::ID(2195247).address() + 0x50;
		RELEX::WriteSafeNop(offset, 0xFE);
		RELEX::WriteSafe(offset, { 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x89, 0xF9, 0x48, 0x89, 0xFA,
			0x90, 0x90, 0x90, 0x90, 0x90, 0x48, 0x83, 0xC4, 0x20 });
		RELEX::DetourCall(offset + 10, (uintptr_t)&AddSomethingBullshit);*/

		RELEX::DetourJump(REL::Offset(0x3A5B90).address(), (uintptr_t)&Workshop__Workbench__StoreAll);

		/*auto patch = new detail::AddLLLLPatch(
			(uintptr_t)GetDefaultObjectFromDefaultManager,
			(uintptr_t)&RE::Workshop::WorkshopCanShowRecipe,
			(uintptr_t)AddSomethingBullshitToMenu);
		RELEX::DetourCall(offset + 6, (uintptr_t)patch->getCode());*/
		//// Icon Lag Fix
		//if (RELEX::IsRuntimeOG())
		//{
		//	std::array Payload{ std::uint8_t{0x90}, std::uint8_t{0x90}, std::uint8_t{0x90} };
		//	REL::WriteSafe(HookIconLoadLagTarget.address(), Payload.data(), Payload.size());
		//}
		//else
		//{
		//	std::array Payload{ std::uint8_t{0x66}, std::uint8_t{0x90} };
		//	REL::WriteSafe(HookIconLoadLagTarget.address(), Payload.data(), Payload.size());
		//}

		return true;
	}

	bool ModuleFasterWorkshop::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
	/*	if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			fasterWorkshopDetail::ClearBuiltMap();
		}*/

		return true;
	}

	bool ModuleFasterWorkshop::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
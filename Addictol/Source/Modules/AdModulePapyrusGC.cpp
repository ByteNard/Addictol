#include <Modules/AdModulePapyrusGC.h>
#include <AdUtils.h>

#include <RE/B/BSScript_Array.h>
#include <RE/B/BSScript_Struct.h>
#include <RE/B/BSTArray.h>
#include <RE/B/BSTSmartPointer.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPapyrusGCBug{ "Fixes"sv, "bPapyrusGCBug"sv, true };

	namespace papyrusGCDetail
	{
		// AE IDs determined via:
		//   GetTimer, FrequencyMS: OG->MainList->AE address library chain (confirmed)
		//   ProcessStructCleanup:  OG->MainList->AE chain, verified against AE PDB symbol
		//   ProcessArrayCleanup:   Template instantiation proximity (sequential AE ID with
		//                          ProcessStructCleanup, same anonymous namespace, similar size)

		[[nodiscard]] static std::uint64_t GetTimer() noexcept
		{
			static REL::Relocation<std::uint64_t(*)()> func{ REL::ID{ 1300983, 2267999 } };
			return func();
		}

		[[nodiscard]] static float FrequencyMS() noexcept
		{
			static REL::Relocation<float*> var{ REL::ID{ 711608, 2666309 } };
			return *var;
		}

		// O(1) swap-and-pop removal using BSTArray's public API.
		// Equivalent to the engine's BSTArrayRemoveFast but avoids needing per-runtime
		// address library IDs for the template instantiations.
		template<typename T>
		static void ArrayRemoveUnordered(RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t a_index) noexcept
		{
			if (a_index != a_elements.size() - 1)
			{
				a_elements[a_index] = std::move(a_elements.back());
			}
			a_elements.pop_back();
		}

		// Replaces the buggy ProcessArrayCleanup/ProcessStructCleanup loop logic.
		// Original bug: loop used index == NextIndexToClean as exit condition, which could
		// match on the very first deletion, causing premature exit after collecting one object.
		// Fix: use a counter-based approach to ensure the full time budget is utilized.
		// Credit: Nukem9 (https://github.com/Nukem9/fallout4-gc-bug-fix)
		template<typename T>
		static bool ProcessCleanup(float a_timeBudget, RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t& a_nextIndex, [[maybe_unused]] void* a_unused) noexcept
		{
			bool didGC = false;

			const std::uint64_t startTime = GetTimer();
			const std::uint64_t maxEndTime = startTime + static_cast<std::uint64_t>(FrequencyMS() * a_timeBudget);

			std::uint32_t maximumElementsChecked = a_elements.size();
			std::uint32_t index = (a_nextIndex < a_elements.size()) ? a_nextIndex : a_elements.size() - 1;

			while (!a_elements.empty())
			{
				if (a_elements[index]->QRefCount() == 1)
				{
					didGC = true;
					ArrayRemoveUnordered(a_elements, index);
				}

				if (index-- == 0)
					index = a_elements.size() - 1;

				if (a_timeBudget > 0 && GetTimer() >= maxEndTime)
					break;

				if (maximumElementsChecked-- == 1)
					break;
			}

			a_nextIndex = index;
			return didGC;
		}

		static void Install() noexcept
		{
			auto& trampoline = REL::GetTrampoline();

			REL::Relocation<std::uintptr_t> targetArray{ REL::ID{ 1068525, 2315348 } };
			trampoline.write_jmp<5>(targetArray.address(), ProcessCleanup<RE::BSScript::Array>);

			REL::Relocation<std::uintptr_t> targetStruct{ REL::ID{ 1466234, 2315349 } };
			trampoline.write_jmp<5>(targetStruct.address(), ProcessCleanup<RE::BSScript::Struct>);
		}
	}

	ModulePapyrusGC::ModulePapyrusGC() :
		Module("PapyrusGC", &bFixesPapyrusGCBug)
	{}

	bool ModulePapyrusGC::DoQuery() const noexcept
	{
		return RELEX::IsRuntimeOG() || RELEX::IsRuntimeAE();
	}

	bool ModulePapyrusGC::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoad)
		{
			papyrusGCDetail::Install();
		}

		return true;
	}

	bool ModulePapyrusGC::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePapyrusGC::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}

#include <Modules/AdModulePapyrusGC.h>
#include <AdUtils.h>

#include <cstring>

#include <RE/B/BSScript_Array.h>
#include <RE/B/BSScript_Struct.h>
#include <RE/B/BSTArray.h>
#include <RE/B/BSTSmartPointer.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPapyrusGCBug{ "Fixes"sv, "bPapyrusGCBug"sv, true };

	namespace papyrusGCDetail
	{
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

		// OG: calls the engine's own BSTArrayRemoveFast (Nukem9's original approach).
		template<typename T>
		static void BSTArrayRemoveFastOG(RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t a_index) noexcept
		{
			using RemoveFn = void(*)(RE::BSTArray<RE::BSTSmartPointer<T>>&, std::uint32_t, std::uint32_t);

			if constexpr (std::is_same_v<T, RE::BSScript::Array>)
			{
				static REL::Relocation<RemoveFn> func{ REL::ID(430486) };
				func(a_elements, a_index, 1);
			}
			else if constexpr (std::is_same_v<T, RE::BSScript::Struct>)
			{
				static REL::Relocation<RemoveFn> func{ REL::ID(1294396) };
				func(a_elements, a_index, 1);
			}
		}

		// AE: OG's typed BSTArrayRemoveFast does not exist on AE. We cannot use
		// CommonLibF4's BSTSmartPointer release path because its delete invokes a
		// trivial compiler-generated destructor that doesn't clean up engine-
		// allocated internals (BSTArray buffers, nested smart pointers, etc.).
		// Instead: steal the pointer, compact via raw memcpy, and release through
		// the engine's own typed destructor+deallocator.
		template<typename T>
		static void BSTArrayRemoveFastAE(RE::BSTArray<RE::BSTSmartPointer<T>>& a_elements, std::uint32_t a_index) noexcept
		{
			// Steal raw pointer and nullify the smart pointer slot so TryDetach
			// (and thus CommonLibF4's delete) never fires on the doomed element.
			T* doomed = a_elements[a_index].get();
			std::memset(&a_elements[a_index], 0, sizeof(RE::BSTSmartPointer<T>));

			// Compact: relocate last element into the gap via raw copy, then
			// zero the vacated last slot. No ref counting triggered.
			const std::uint32_t lastIndex = a_elements.size() - 1;
			if (a_index != lastIndex)
			{
				std::memcpy(&a_elements[a_index], &a_elements[lastIndex], sizeof(RE::BSTSmartPointer<T>));
				std::memset(&a_elements[lastIndex], 0, sizeof(RE::BSTSmartPointer<T>));
			}
			a_elements.pop_back();

			// Decrement refcount from 1 to 0 (matching the engine's own pattern
			// where ProcessCleanup atomically decrements before calling release).
			(void)doomed->DecRef();

			// Release through the engine's proper destructor/deallocator.
			using ReleaseFn = void(*)(T*);
			if constexpr (std::is_same_v<T, RE::BSScript::Array>)
			{
				static REL::Relocation<ReleaseFn> func{ REL::ID(2314448) };
				func(doomed);
			}
			else if constexpr (std::is_same_v<T, RE::BSScript::Struct>)
			{
				static REL::Relocation<ReleaseFn> func{ REL::ID(2314957) };
				func(doomed);
			}
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
					if (RELEX::IsRuntimeOG())
						BSTArrayRemoveFastOG(a_elements, index);
					else
						BSTArrayRemoveFastAE(a_elements, index);
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

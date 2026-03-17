#include <Modules/AdModulePapyrusGC.h>

#include <RE/B/BSScript_Internal_PapyrusObjects.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPapyrusGCBug{ "Fixes"sv, "bPapyrusGCBug"sv, true };

	class hkProcessArrayCleanup :
		public REX::Singleton<hkProcessArrayCleanup>
	{
	public:
		static void Install()
		{
			// TODO: Query ProcessArrayCleanup addresses for OG/NG/AE
			// TODO: Apply Nukem9's patched loop logic via Xbyak
		}

	private:
		// Placeholder for hook implementation
	};

	class hkProcessStructCleanup :
		public REX::Singleton<hkProcessStructCleanup>
	{
	public:
		static void Install()
		{
			// TODO: Query ProcessStructCleanup addresses for OG/NG/AE
			// TODO: Apply Nukem9's patched loop logic via Xbyak
		}

	private:
		// Placeholder for hook implementation
	};

	ModulePapyrusGC::ModulePapyrusGC() :
		Module("PapyrusGC", &bFixesPapyrusGCBug)
	{}

	bool ModulePapyrusGC::DoQuery() const noexcept
	{
		// TODO: Verify ProcessArrayCleanup and ProcessStructCleanup are available
		return true;
	}

	bool ModulePapyrusGC::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoad)
		{
			hkProcessArrayCleanup::Install();
			hkProcessStructCleanup::Install();
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

#include <Modules/AdModuleCOMInit.h>
#include <AdUtils.h>

#include <windows.h>
#include <combaseapi.h>

#undef MAX_PATH
#undef MEM_RELEASE

#define AD_NOMESSAGE_CHECKINTERNETACCESS 1

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesCOMInit{ "Patches"sv, "bCOMInit"sv, true };

	namespace detail
	{
		decltype(&CoInitializeEx) CoInitializeExOrig{ nullptr };

		static REX::W32::HRESULT CoInitializeEx([[maybe_unused]] LPVOID pvReserved, [[maybe_unused]] DWORD dwCoInit)
		{
			// analog CoInitialize(nullptr)
			return (REX::W32::HRESULT)CoInitializeExOrig(nullptr, COINIT_APARTMENTTHREADED);
		}
	}

	ModuleCOMInit::ModuleCOMInit() :
		Module("COM Init", &bPatchesCOMInit)
	{}

	bool ModuleCOMInit::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleCOMInit::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto dll = GetModuleHandleA("Ole32.dll");
		if (!dll)
		{
			REX::INFO("No found Ole32.dll");
			return false;
		}

		auto func = GetProcAddress(dll, "CoInitializeEx");
		if (!func)
		{
			REX::INFO("No found CoInitializeEx() in Ole32.dll");
			return false;
		}
		
		*(uintptr_t*)(&detail::CoInitializeExOrig) =
			RELEX::DetourJump((uintptr_t)func, (uintptr_t)&detail::CoInitializeEx);

		if (!detail::CoInitializeExOrig)
		{
			REX::INFO("Fatal patching Ole32.dll");
			return false;
		}

		return true;
	}

	bool ModuleCOMInit::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleCOMInit::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
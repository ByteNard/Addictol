#pragma once

#include <string>

#include <REX/REX.h>
#include <F4SE/F4SE.h>

namespace Addictol
{
	using namespace std::literals;

	// Hooks GetProcAddress in F4SE's import address table to transparently
	// time every other F4SE plugin's F4SEPlugin_Query and F4SEPlugin_Load
	// calls.  Must be installed during the preload stage so the hook is in
	// place before F4SE begins dispatching Load calls to other plugins.
	//
	// Thread-safety: F4SE processes plugins sequentially on the main thread,
	// so the hook uses simple static state with no locking.
	class ProfilerDLL :
		public REX::Singleton<ProfilerDLL>
	{
		bool m_installed{ false };

		ProfilerDLL(const ProfilerDLL&) = delete;
		ProfilerDLL& operator=(const ProfilerDLL&) = delete;
	public:
		ProfilerDLL() = default;
		virtual ~ProfilerDLL() = default;

		// Hook GetProcAddress in F4SE's IAT.
		//
		// a_f4se  The F4SE interface pointer received during plugin init.
		//         Used solely to resolve F4SE's HMODULE via
		//         GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS).
		//         A PreLoadInterface* reinterpret_cast'd to LoadInterface*
		//         is valid here -- only the address matters.
		void Install(const F4SE::LoadInterface* a_f4se) noexcept;

		// Returns true after a successful Install().
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }

		// Extract a "major.minor.build.sub" file-version string from a PE
		// image on disk.  Returns an empty string on any failure.
		[[nodiscard]] static std::string GetFileVersionString(const char* a_path) noexcept;
	};
}

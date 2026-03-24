#include <AdProfilerDLL.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>

#include <Windows.h>

// Resolve macro conflicts between Windows.h and CommonLibF4 identifiers.
#undef ERROR
#undef MEM_RELEASE
#undef MAX_PATH
#undef PAGE_EXECUTE_READWRITE

#include <REX\W32\KERNEL32.h>
#include <REX\W32\VERSION.h>

#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>

namespace Addictol
{
	// ======================================================================
	//  Static hook state
	// ======================================================================
	//
	// F4SE processes plugins sequentially on the main thread:
	//
	//   Phase 1 (OG only) -- for each DLL: GetProcAddress("F4SEPlugin_Query")
	//                        then call it, then next DLL.
	//   Phase 2           -- for each DLL: GetProcAddress("F4SEPlugin_Load")
	//                        then call it, then next DLL.
	//
	// Between a GetProcAddress return and the subsequent wrapper call, no
	// other GetProcAddress for a plugin export can happen.  This lets us use
	// simple static variables for the "currently active" context.
	// ======================================================================

	// Original kernel32!GetProcAddress, saved when the IAT hook is installed.
	using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
	static GetProcAddress_t s_origGetProcAddress = nullptr;

	// Addictol's own HMODULE -- we skip self-profiling.
	static HMODULE s_ownModule = nullptr;

	// In-progress DLLProfileEntry objects, keyed by HMODULE.
	// An entry is created when we first intercept a plugin export for a
	// given module and is finalised (moved into ProfilerCore) once the
	// Load wrapper completes.  The map is needed because OG F4SE queries
	// ALL plugins before loading any, so Query and Load for the same DLL
	// happen in separate phases.
	static std::unordered_map<HMODULE, DLLProfileEntry> s_pendingEntries;

	// Context for the wrapper that is *about* to be called.
	// Set inside Hooked_GetProcAddress, consumed inside Wrapper_*.
	static HMODULE s_activeModule = nullptr;
	static FARPROC s_origQueryFn  = nullptr;
	static FARPROC s_origLoadFn   = nullptr;

	// ======================================================================
	//  Helpers
	// ======================================================================

	static std::string GetModulePath(HMODULE a_module) noexcept
	{
		char buf[4096]{};
		if (REX::W32::GetModuleFileNameA(
			reinterpret_cast<REX::W32::HMODULE>(a_module), buf,
			static_cast<std::uint32_t>(sizeof(buf))))
		{
			return buf;
		}
		return {};
	}

	static std::string ExtractFileName(const std::string& a_path) noexcept
	{
		auto pos = a_path.find_last_of("\\/");
		return (pos != std::string::npos) ? a_path.substr(pos + 1) : a_path;
	}

	// Lazily populate a DLLProfileEntry for |a_module|.
	static DLLProfileEntry& GetOrCreateEntry(HMODULE a_module) noexcept
	{
		auto it = s_pendingEntries.find(a_module);
		if (it != s_pendingEntries.end())
			return it->second;

		auto path = GetModulePath(a_module);
		auto name = ExtractFileName(path);
		auto ver  = ProfilerDLL::GetFileVersionString(path.c_str());

		auto& entry       = s_pendingEntries[a_module];
		entry.dllName     = std::move(name);
		entry.dllPath     = std::move(path);
		entry.fileVersion = std::move(ver);
		return entry;
	}

	// ======================================================================
	//  Timing wrappers
	// ======================================================================
	//
	// F4SE plugin export signatures (x64 -- single calling convention):
	//
	//   bool F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo*)
	//   bool F4SEPlugin_Load (const F4SE::LoadInterface*)
	//
	// We use opaque void* parameters so the wrappers do not depend on the
	// concrete F4SE interface types and can be cast to/from FARPROC freely.
	// ======================================================================

	static bool Wrapper_Query(const void* a_f4se, void* a_info) noexcept
	{
		// Capture context -- valid because F4SE is single-threaded here.
		HMODULE mod    = s_activeModule;
		auto    origFn = reinterpret_cast<bool(*)(const void*, void*)>(s_origQueryFn);

		auto start  = std::chrono::high_resolution_clock::now();
		bool result = origFn(a_f4se, a_info);
		auto end    = std::chrono::high_resolution_clock::now();

		double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

		auto it = s_pendingEntries.find(mod);
		if (it != s_pendingEntries.end())
		{
			it->second.queryMs = elapsed;
			REX::INFO("[Profiler] DLL Query: {} ({:.2f} ms)"sv,
				it->second.dllName, elapsed);
		}

		return result;
	}

	static bool Wrapper_Load(const void* a_f4se) noexcept
	{
		HMODULE mod    = s_activeModule;
		auto    origFn = reinterpret_cast<bool(*)(const void*)>(s_origLoadFn);

		auto start  = std::chrono::high_resolution_clock::now();
		bool result = origFn(a_f4se);
		auto end    = std::chrono::high_resolution_clock::now();

		double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

		auto it = s_pendingEntries.find(mod);
		if (it != s_pendingEntries.end())
		{
			it->second.loadMs = elapsed;
			REX::INFO("[Profiler] DLL Load: {} ({:.2f} ms)"sv,
				it->second.dllName, elapsed);

			// Finalise -- move completed entry into ProfilerCore.
			ProfilerCore::GetSingleton()->AddDLLEntry(std::move(it->second));
			s_pendingEntries.erase(it);
		}

		return result;
	}

	// ======================================================================
	//  Hooked GetProcAddress
	// ======================================================================
	//
	// This function replaces GetProcAddress in F4SE's import table.  Only
	// calls originating from F4SE's own code are routed here -- other
	// modules' IATs are unaffected.
	//
	// When F4SE resolves F4SEPlugin_Query or F4SEPlugin_Load, we return a
	// timing wrapper instead of the raw function pointer.  All other
	// lookups are forwarded to the original GetProcAddress unmodified.
	// ======================================================================

	static FARPROC WINAPI Hooked_GetProcAddress(HMODULE a_module, LPCSTR a_procName) noexcept
	{
		// Ordinal imports:  HIWORD == 0 means a_procName is an ordinal, not
		// a string.  Pass through immediately.
		if ((reinterpret_cast<std::uintptr_t>(a_procName) >> 16) == 0)
			return s_origGetProcAddress(a_module, a_procName);

		// Never profile our own module.
		if (a_module == s_ownModule)
			return s_origGetProcAddress(a_module, a_procName);

		// --- F4SEPlugin_Query (OG path) ---------------------------------
		if (std::strcmp(a_procName, "F4SEPlugin_Query") == 0)
		{
			FARPROC original = s_origGetProcAddress(a_module, a_procName);
			if (original)
			{
				s_activeModule = a_module;
				s_origQueryFn  = original;
				GetOrCreateEntry(a_module);
				return reinterpret_cast<FARPROC>(&Wrapper_Query);
			}
			return original; // nullptr -- DLL does not export this symbol
		}

		// --- F4SEPlugin_Load (OG and NG paths) --------------------------
		if (std::strcmp(a_procName, "F4SEPlugin_Load") == 0)
		{
			FARPROC original = s_origGetProcAddress(a_module, a_procName);
			if (original)
			{
				s_activeModule = a_module;
				s_origLoadFn   = original;
				GetOrCreateEntry(a_module);
				return reinterpret_cast<FARPROC>(&Wrapper_Load);
			}
			return original;
		}

		// Everything else -- pass through unchanged.
		return s_origGetProcAddress(a_module, a_procName);
	}

	// ======================================================================
	//  Install
	// ======================================================================

	void ProfilerDLL::Install(const F4SE::LoadInterface* a_f4se) noexcept
	{
		if (m_installed)
			return;

		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive())
			return;

		// ---- Resolve F4SE's HMODULE ------------------------------------
		//
		// The interface pointer passed by F4SE points to a global struct
		// that resides inside the F4SE DLL's data section.  Using
		// GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS lets us derive the
		// owning module without knowing the DLL's file name.
		HMODULE hF4SE = nullptr;
		::GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(a_f4se),
			&hF4SE);

		if (!hF4SE)
		{
			REX::WARN("[Profiler] DLL: Failed to resolve F4SE module handle"sv);
			return;
		}

		// ---- Record our own HMODULE for self-skip ----------------------
		::GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&Hooked_GetProcAddress),
			&s_ownModule);

		// ---- Hook GetProcAddress in F4SE's IAT -------------------------
		//
		// RELEX::DetourIAT walks the PE import directory of the target
		// module (hF4SE), locates the IAT entry for kernel32!GetProcAddress,
		// and overwrites it with our Hooked_GetProcAddress.  The return
		// value is the original function pointer.
		auto original = RELEX::DetourIAT(
			reinterpret_cast<uintptr_t>(hF4SE),
			"kernel32.dll",
			"GetProcAddress",
			reinterpret_cast<uintptr_t>(&Hooked_GetProcAddress));

		if (!original)
		{
			REX::WARN("[Profiler] DLL: Failed to hook GetProcAddress in F4SE IAT"sv);
			return;
		}

		s_origGetProcAddress = reinterpret_cast<GetProcAddress_t>(original);
		m_installed = true;

		REX::INFO("[Profiler] DLL: GetProcAddress hook installed in F4SE IAT"sv);
		profiler->MarkPhase("DLLProfilerInstalled"sv);
	}

	// ======================================================================
	//  Version extraction
	// ======================================================================
	//
	// Queries the root VS_FIXEDFILEINFO block ("\\") of a PE file's
	// version resource.  This is more reliable than the StringFileInfo
	// approach because it does not depend on a specific language/codepage.
	//
	// The VS_FIXEDFILEINFO layout (all DWORD, naturally aligned):
	//   [0] dwSignature           = 0xFEEF04BD
	//   [1] dwStrucVersion
	//   [2] dwFileVersionMS       = (major << 16) | minor
	//   [3] dwFileVersionLS       = (build << 16) | sub
	//   ... (remaining fields not needed)
	// ======================================================================

	std::string ProfilerDLL::GetFileVersionString(const char* a_path) noexcept
	{
		if (!a_path || !*a_path)
			return {};

		std::uint32_t dummy = 0;
		std::uint32_t verSize = REX::W32::GetFileVersionInfoSizeA(a_path, &dummy);
		if (!verSize)
			return {};

		auto verBuf = std::make_unique<char[]>(verSize);
		if (!REX::W32::GetFileVersionInfoA(a_path, 0, verSize, verBuf.get()))
			return {};

		void*         infoPtr = nullptr;
		std::uint32_t infoLen = 0;
		if (!REX::W32::VerQueryValueA(verBuf.get(), "\\", &infoPtr, &infoLen))
			return {};

		// VS_FIXEDFILEINFO is 13 DWORDs (52 bytes).
		constexpr std::uint32_t kFixedInfoMinSize = 52;
		if (!infoPtr || infoLen < kFixedInfoMinSize)
			return {};

		auto info = static_cast<const std::uint32_t*>(infoPtr);

		// Validate the magic signature.
		constexpr std::uint32_t kSignature = 0xFEEF04BD;
		if (info[0] != kSignature)
			return {};

		// info[2] = dwFileVersionMS,  info[3] = dwFileVersionLS
		std::uint32_t ms = info[2];
		std::uint32_t ls = info[3];

		return std::format("{}.{}.{}.{}",
			(ms >> 16) & 0xFFFF,
			(ms)       & 0xFFFF,
			(ls >> 16) & 0xFFFF,
			(ls)       & 0xFFFF);
	}
}

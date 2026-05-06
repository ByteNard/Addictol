#include <Modules/AdModuleBethesdaNetCrash.h>
#include <AdUtils.h>

#include <cerrno>
#include <cstring>
#include <Windows.h>

// Port of SSE Engine Fixes' BethesdaNetCrash.
// Source: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/bethesda_net_crash.h
//
// Bug: At startup the game queries Bethesda.net (Mods / Creation Club menus). The
// HTTP response headers occasionally contain non-ASCII bytes that don't match
// the user's current Windows ANSI codepage. The CRT's locale-aware wcsrtombs_s
// returns errno EILSEQ in that situation; the caller doesn't error-check the
// return code and crashes shortly after.
// Symptom: hard CTD on launch, common on non-English Windows installs (CN, JP,
// RU, KR locales especially).
//
// Fix: IAT-hook wcsrtombs_s in api-ms-win-crt-convert-l1-1-0.dll with a
// locale-independent replacement built on WideCharToMultiByte(CP_UTF8, ...).
// Returns clean errno_t (0) so the caller proceeds normally.

namespace Addictol
{
	static REX::TOML::Bool<> bFixesBethesdaNetCrash{ "Fixes"sv, "bBethesdaNetCrash"sv, true };

	namespace bethesdaNetCrashDetail
	{
		using wcsrtombs_s_t = errno_t (__cdecl*)(
			std::size_t*    pReturnValue,
			char*           mbstr,
			std::size_t     sizeInBytes,
			const wchar_t** wcstr,
			std::size_t     count,
			std::mbstate_t* mbstate);

		static wcsrtombs_s_t g_origWcsrtombsS{ nullptr };

		static errno_t __cdecl Hook_wcsrtombs_s(
			std::size_t*    pReturnValue,
			char*           mbstr,
			std::size_t     sizeInBytes,
			const wchar_t** wcstr,
			std::size_t     count,
			[[maybe_unused]] std::mbstate_t* mbstate) noexcept
		{
			if (!wcstr || !*wcstr)
			{
				if (pReturnValue)
					*pReturnValue = 0;
				if (mbstr && sizeInBytes > 0)
					mbstr[0] = '\0';
				return 0;
			}

			const wchar_t* src = *wcstr;

			// Determine source length up to either count or the terminating NUL.
			// Pass count = -1 to WideCharToMultiByte (NUL-terminated mode) when
			// the caller supplied count == _TRUNCATE / SIZE_MAX.
			int srcLen = -1;
			if (count != static_cast<std::size_t>(-1))
			{
				std::size_t n = 0;
				while (n < count && src[n] != L'\0')
					++n;
				srcLen = static_cast<int>(n);
			}

			// Probe required buffer size.
			const int needed = WideCharToMultiByte(
				CP_UTF8, 0, src, srcLen, nullptr, 0, nullptr, nullptr);
			if (needed <= 0)
			{
				if (pReturnValue)
					*pReturnValue = 0;
				if (mbstr && sizeInBytes > 0)
					mbstr[0] = '\0';
				return 0;
			}

			if (mbstr == nullptr)
			{
				// Caller is asking how big a buffer it needs.
				if (pReturnValue)
					*pReturnValue = static_cast<std::size_t>(needed);
				return 0;
			}

			if (static_cast<std::size_t>(needed) > sizeInBytes)
			{
				// Truncate: write what fits, NUL-terminate, claim success
				// (matches SSE behavior: don't propagate EILSEQ to a caller
				// that won't handle it).
				const int written = WideCharToMultiByte(
					CP_UTF8, 0, src, srcLen,
					mbstr, static_cast<int>(sizeInBytes), nullptr, nullptr);
				const std::size_t writtenSz =
					(written > 0) ? static_cast<std::size_t>(written) : 0;
				if (writtenSz < sizeInBytes)
					mbstr[writtenSz] = '\0';
				else
					mbstr[sizeInBytes - 1] = '\0';
				if (pReturnValue)
					*pReturnValue = writtenSz;
				return 0;
			}

			const int written = WideCharToMultiByte(
				CP_UTF8, 0, src, srcLen, mbstr, static_cast<int>(sizeInBytes),
				nullptr, nullptr);
			const std::size_t writtenSz =
				(written > 0) ? static_cast<std::size_t>(written) : 0;
			if (writtenSz < sizeInBytes)
				mbstr[writtenSz] = '\0';

			if (pReturnValue)
				*pReturnValue = writtenSz;
			return 0;
		}
	}

	ModuleBethesdaNetCrash::ModuleBethesdaNetCrash() :
		Module("Bethesda.net Crash", &bFixesBethesdaNetCrash)
	{}

	bool ModuleBethesdaNetCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleBethesdaNetCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// IAT-hook wcsrtombs_s in the game's main module. The CRT API set DLL is
		// "api-ms-win-crt-convert-l1-1-0.dll" - that's the import name in the
		// Fallout4.exe IAT. (Walking the IAT for the main exe = pass NULL to
		// GetModuleHandle, which is what RELEX::DetourIAT does internally.)
		auto original = RELEX::DetourIAT(
			"api-ms-win-crt-convert-l1-1-0.dll",
			"wcsrtombs_s",
			reinterpret_cast<std::uintptr_t>(&bethesdaNetCrashDetail::Hook_wcsrtombs_s));

		if (!original)
		{
			REX::WARN("Bethesda.net Crash: failed to locate wcsrtombs_s in IAT (already patched, or import name differs)."sv);
			return false;
		}

		bethesdaNetCrashDetail::g_origWcsrtombsS =
			reinterpret_cast<bethesdaNetCrashDetail::wcsrtombs_s_t>(original);

		return true;
	}

	bool ModuleBethesdaNetCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleBethesdaNetCrash::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}

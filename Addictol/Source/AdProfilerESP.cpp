#include <AdProfilerESP.h>
#include <AdProfilerCore.h>
#include <AdUtils.h>

namespace
{
	using namespace Addictol;

	// -----------------------------------------------------------------
	// SEH-isolated helpers
	// These must NOT contain local C++ objects with non-trivial destructors
	// (MSVC C2712: Cannot use __try in functions that require object unwinding)
	// -----------------------------------------------------------------

	// Resolves a REL::ID to an absolute address, catching structured exceptions.
	// Returns 0 on failure.
	uintptr_t SafeResolveID(std::uint32_t a_id) noexcept
	{
		uintptr_t result = 0;
		__try
		{
			result = REL::Relocation<uintptr_t>(REL::ID(a_id)).address();
		}
		__except (1)
		{
			result = 0;
		}
		return result;
	}

	// Calls the original CompileFiles trampoline under SEH protection.
	// Returns: 0 = exception caught, 1 = original returned true, 2 = original returned false
	int SafeCallCompileFiles(bool(__fastcall* a_original)(void*), void* a_this) noexcept
	{
		__try
		{
			return a_original(a_this) ? 1 : 2;
		}
		__except (1)
		{
			return 0;
		}
	}

	// Calls the original ConstructObjectList trampoline under SEH protection.
	// Returns false if an exception was caught.
	bool SafeCallConstructObjectList(void(__fastcall* a_original)(void*, void*), void* a_this, void* a_file) noexcept
	{
		__try
		{
			a_original(a_this, a_file);
			return true;
		}
		__except (1)
		{
			return false;
		}
	}

	// Calls the original InitAllForms trampoline under SEH protection.
	bool SafeCallInitAllForms(void(__fastcall* a_original)(void*), void* a_this) noexcept
	{
		__try
		{
			a_original(a_this);
			return true;
		}
		__except (1)
		{
			return false;
		}
	}

	// Scans memory starting at a_funcBase for E8 (near relative CALL) instructions.
	// Writes resolved CallSiteInfo entries into a_outBuf (up to a_maxResults).
	// Returns the number of valid call sites found.
	std::size_t ScanCallSitesImpl(
		uintptr_t a_funcBase, std::size_t a_maxBytes,
		ESPProfiler::CallSiteInfo* a_outBuf, std::size_t a_maxResults) noexcept
	{
		std::size_t count = 0;

		__try
		{
			for (std::size_t i = 0; i + 5 <= a_maxBytes && count < a_maxResults; ++i)
			{
				if (*reinterpret_cast<const uint8_t*>(a_funcBase + i) != 0xE8)
					continue;

				// Resolve relative CALL: target = (call_addr + 5) + int32_displacement
				auto disp = *reinterpret_cast<const std::int32_t*>(a_funcBase + i + 1);
				uintptr_t site = a_funcBase + i;
				uintptr_t target = site + 5 + static_cast<std::intptr_t>(disp);

				// Validate: target should be within ±1 GB of call site and above low memory.
				// This filters out false positives where 0xE8 appears as part of an immediate
				// operand in another instruction rather than as a CALL opcode.
				auto diff = static_cast<std::intptr_t>(target - site);
				if (target > 0x10000 && diff > -0x40000000LL && diff < 0x40000000LL)
					a_outBuf[count++] = { site, target, i };
			}
		}
		__except (1) {}

		return count;
	}
}

namespace Addictol
{
	// -----------------------------------------------------------------
	// TOML configuration
	// -----------------------------------------------------------------
	static REX::TOML::F64<> fWarnThresholdMs{ "Profiler"sv, "fWarnThresholdMs"sv, 500.0 };
	static REX::TOML::F64<> fCritThresholdMs{ "Profiler"sv, "fCritThresholdMs"sv, 2000.0 };
	static REX::TOML::Bool<> bESPSubHooks{ "Profiler"sv, "bESPSubHooks"sv, false };

	// -----------------------------------------------------------------
	// Scanning parameters
	// -----------------------------------------------------------------

	// Maximum bytes to scan from CompileFiles entry point for CALL discovery.
	// CompileFiles is typically 2000–4000 bytes; 4 KB is a safe upper bound.
	static constexpr std::size_t kMaxScanBytes = 0x1000;

	// Version-specific call-site indices within CompileFiles body.
	// Each value selects the Nth E8 CALL instruction (0-based) found during scanning.
	//
	// To determine correct values for a new game version:
	//   1. Set bProfiler=true in Addictol.toml
	//   2. Launch the game and check Addictol.log for the "[Profiler/ESP]" call-site dump
	//   3. Identify ConstructObjectList (2-param call in the file loop — sets RDX to TESFile*)
	//      and InitAllForms (single-param call after the loop, one of the last calls)
	//   4. Update the indices below and rebuild
	//
	// OG (1.10.163.0):
	static constexpr std::size_t kOG_ConstructObjectListIdx = 5;
	static constexpr std::size_t kOG_InitAllFormsIdx        = 12;
	// NG (1.10.984+):
	static constexpr std::size_t kNG_ConstructObjectListIdx = 5;
	static constexpr std::size_t kNG_InitAllFormsIdx        = 12;

	// REL::ID for TESDataHandler::CompileFiles
	// OG (1.10.163): 57137  — prologue: PUSH RBX; PUSH RSI; PUSH RDI; PUSH R12-R15; SUB RSP
	// NG (1.10.984+): 0     — prologue: MOV RAX,RSP (Detours handles instruction relocation)
	//                         Set to actual NG ID once determined from address database.
	//                         Set to 0 to disable NG profiling until the ID is known.
	static constexpr std::uint32_t kCompileFilesID_OG = 57137;
	static constexpr std::uint32_t kCompileFilesID_NG = 0;

	// TESFile::filename — inline char[260] at this offset from TESFile*
	static constexpr uintptr_t kTESFileNameOffset = 0x70;

	// -----------------------------------------------------------------
	// Call-site scanning
	// -----------------------------------------------------------------

	std::vector<ESPProfiler::CallSiteInfo> ESPProfiler::ScanCallSites(
		uintptr_t a_funcBase, std::size_t a_maxBytes) noexcept
	{
		// Stack buffer sized for the maximum plausible number of CALLs in 4 KB of code.
		// 256 * sizeof(CallSiteInfo) ≈ 6 KB — acceptable for a one-time init call.
		static constexpr std::size_t kMaxResults = 256;
		CallSiteInfo buffer[kMaxResults]{};

		auto count = ScanCallSitesImpl(a_funcBase, a_maxBytes, buffer, kMaxResults);
		return { buffer, buffer + count };
	}

	// -----------------------------------------------------------------
	// TESFile filename extraction
	// -----------------------------------------------------------------

	const char* ESPProfiler::GetTESFileName(void* a_file) noexcept
	{
		if (!a_file)
			return nullptr;

		// No C++ objects here — __try is safe.
		__try
		{
			auto addr = reinterpret_cast<uintptr_t>(a_file) + kTESFileNameOffset;
			const char* name = reinterpret_cast<const char*>(addr);

			// Sanity: first character must be printable ASCII
			if (name[0] > 0x1F && name[0] < 0x7F)
				return name;
		}
		__except (1)
		{
		}

		return nullptr;
	}

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::CompileFiles
	// Wraps the entire plugin compilation pass and records total time.
	// -----------------------------------------------------------------

	bool __fastcall ESPProfiler::HookCompileFiles(void* a_this) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		// Passthrough if profiler became inactive or original was not captured
		if (!core->IsActive() || !OriginalCompileFiles)
			return OriginalCompileFiles ? OriginalCompileFiles(a_this) : false;

		REX::INFO("[Profiler/ESP] CompileFiles entered (this={:016X})"sv,
			reinterpret_cast<uintptr_t>(a_this));

		// Reset the per-file counter for this compilation pass
		GetSingleton()->m_currentFileIndex = 0;

		core->MarkPhase("CompileFiles_Begin"sv);

		auto start = std::chrono::high_resolution_clock::now();
		int callResult = SafeCallCompileFiles(OriginalCompileFiles, a_this);
		auto end = std::chrono::high_resolution_clock::now();
		double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

		if (callResult == 0)
		{
			REX::ERROR("[Profiler/ESP] CompileFiles CRASHED in original function! "
				"SEH caught the exception. Returning false."sv);
			return false;
		}

		bool result = (callResult == 1);
		core->SetTotalCompileTime(totalMs);
		core->MarkPhase("CompileFiles_End"sv);

		REX::INFO("[Profiler/ESP] CompileFiles completed in {:.1f} ms ({:.2f} s)"sv,
			totalMs, totalMs / 1000.0);

		return result;
	}

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::ConstructObjectList
	// Times per-file record loading and feeds ESPProfileEntry to core.
	// -----------------------------------------------------------------

	void __fastcall ESPProfiler::HookConstructObjectList(void* a_this, void* a_file) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();
		auto* self = GetSingleton();

		// Passthrough if profiler is inactive
		if (!core->IsActive() || !OriginalConstructObjectList)
		{
			if (OriginalConstructObjectList)
				OriginalConstructObjectList(a_this, a_file);
			return;
		}

		ESPProfileEntry entry;
		entry.loadOrderIndex = self->m_currentFileIndex++;

		// Extract filename from TESFile* at offset +0x70 (SEH-protected)
		const char* name = GetTESFileName(a_file);
		entry.filename = name ? name : "(unknown)";

		// Time the record construction phase (the heaviest per-file operation)
		auto start = std::chrono::high_resolution_clock::now();
		bool ok = SafeCallConstructObjectList(OriginalConstructObjectList, a_this, a_file);
		auto end = std::chrono::high_resolution_clock::now();
		entry.constructMs = std::chrono::duration<double, std::milli>(end - start).count();

		if (!ok)
		{
			REX::ERROR("[Profiler/ESP] ConstructObjectList CRASHED on file [{}] {}!"sv,
				entry.loadOrderIndex, entry.filename);
			return;
		}

		entry.totalMs = entry.constructMs;

		// Threshold-based warnings for slow-loading files
		const double critMs = fCritThresholdMs.GetValue();
		const double warnMs = fWarnThresholdMs.GetValue();

		if (entry.constructMs >= critMs)
		{
			REX::WARN("[Profiler/ESP] CRITICAL: [{:3d}] {:40s} {:.1f} ms"sv,
				entry.loadOrderIndex, entry.filename, entry.constructMs);
		}
		else if (entry.constructMs >= warnMs)
		{
			REX::WARN("[Profiler/ESP] SLOW: [{:3d}] {:40s} {:.1f} ms"sv,
				entry.loadOrderIndex, entry.filename, entry.constructMs);
		}

		core->AddESPEntry(std::move(entry));
	}

	// -----------------------------------------------------------------
	// Hook: TESDataHandler::InitAllForms
	// Times the post-load form initialization pass.
	// -----------------------------------------------------------------

	void __fastcall ESPProfiler::HookInitAllForms(void* a_this) noexcept
	{
		auto* core = ProfilerCore::GetSingleton();

		// Passthrough if profiler is inactive
		if (!core->IsActive() || !OriginalInitAllForms)
		{
			if (OriginalInitAllForms)
				OriginalInitAllForms(a_this);
			return;
		}

		core->MarkPhase("InitAllForms_Begin"sv);

		auto start = std::chrono::high_resolution_clock::now();
		bool ok = SafeCallInitAllForms(OriginalInitAllForms, a_this);
		auto end = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(end - start).count();

		if (!ok)
		{
			REX::ERROR("[Profiler/ESP] InitAllForms CRASHED! SEH caught the exception."sv);
			return;
		}

		core->SetInitAllFormsTime(ms);
		core->MarkPhase("InitAllForms_End"sv);

		REX::INFO("[Profiler/ESP] InitAllForms completed in {:.1f} ms ({:.2f} s)"sv,
			ms, ms / 1000.0);
	}

	// -----------------------------------------------------------------
	// Installation
	// -----------------------------------------------------------------

	void ESPProfiler::Install() noexcept
	{
		if (m_installed)
			return;

		if (!ProfilerCore::GetSingleton()->IsActive())
		{
			REX::INFO("[Profiler/ESP] Profiler not active, skipping ESP hooks"sv);
			return;
		}

		REX::INFO("[Profiler/ESP] Installing ESP/ESM load profiler hooks..."sv);

		// ---- Step 1: Locate CompileFiles via address library ----

		const bool isOG = RELEX::IsRuntimeOG();
		const std::uint32_t compileFilesID = isOG ? kCompileFilesID_OG : kCompileFilesID_NG;

		if (compileFilesID == 0)
		{
			REX::WARN("[Profiler/ESP] CompileFiles ID not configured for {} runtime, "
				"ESP profiling disabled"sv, isOG ? "OG" : "NG");
			return;
		}

		uintptr_t compileFilesAddr = SafeResolveID(compileFilesID);
		if (!compileFilesAddr)
		{
			REX::ERROR("[Profiler/ESP] Failed to resolve CompileFiles (REL::ID {})"sv,
				compileFilesID);
			return;
		}

		REX::INFO("[Profiler/ESP] CompileFiles at {:016X} (REL::ID {}, {})"sv,
			compileFilesAddr, compileFilesID, isOG ? "OG" : "NG");

		// ---- Step 2: Hook CompileFiles ----
		// OG: standard prologue (PUSH regs; SUB RSP) — DetourJump patches entry point.
		// NG: MOV RAX,RSP prologue — Detours handles instruction relocation to trampoline.

		*(uintptr_t*)(&OriginalCompileFiles) =
			RELEX::DetourJump(compileFilesAddr, (uintptr_t)&HookCompileFiles);

		if (!OriginalCompileFiles)
		{
			REX::ERROR("[Profiler/ESP] Failed to detour CompileFiles"sv);
			return;
		}

		REX::INFO("[Profiler/ESP] CompileFiles hooked (trampoline: {:016X})"sv,
			reinterpret_cast<uintptr_t>(OriginalCompileFiles));

		// ---- Step 3: Scan CompileFiles body for internal CALL sites ----
		// The sub-functions ConstructObjectList and InitAllForms have no address library
		// IDs, so we locate them by scanning CompileFiles for E8 (near CALL) instructions
		// and selecting version-specific indices.

		auto callSites = ScanCallSites(compileFilesAddr, kMaxScanBytes);

		REX::INFO("[Profiler/ESP] Found {} call sites in CompileFiles (scan: {} bytes):"sv,
			callSites.size(), kMaxScanBytes);

		for (std::size_t i = 0; i < callSites.size(); ++i)
		{
			REX::INFO("[Profiler/ESP]   [{:2d}] site={:016X} target={:016X} (offset +0x{:04X})"sv,
				i, callSites[i].site, callSites[i].target, callSites[i].offset);
		}

		// ---- Step 4: Hook ConstructObjectList (call-site patch) ----
		// This is a 2-param call (this, TESFile*) inside CompileFiles' per-file loop.
		// Patching the call-site rather than the function entry avoids prologue concerns
		// and precisely targets calls originating from CompileFiles.
		//
		// CAUTION: bESPSubHooks must be true to enable these hooks.
		// The call-site indices are version-specific and must be verified via the
		// call-site dump before enabling. Wrong indices = wrong function signatures = CTD.

		if (!bESPSubHooks.GetValue())
		{
			REX::INFO("[Profiler/ESP] Sub-hooks disabled (bESPSubHooks=false). "
				"Only CompileFiles timing active. Review call-site dump above to "
				"determine correct indices, then enable bESPSubHooks."sv);
		}
		else
		{
			const std::size_t constructIdx = isOG
				? kOG_ConstructObjectListIdx : kNG_ConstructObjectListIdx;

			if (constructIdx < callSites.size())
			{
				const auto& cs = callSites[constructIdx];

				*(uintptr_t*)(&OriginalConstructObjectList) =
					RELEX::DetourCall(cs.site, (uintptr_t)&HookConstructObjectList);

				if (OriginalConstructObjectList)
				{
					REX::INFO("[Profiler/ESP] ConstructObjectList hooked "
						"(site: {:016X}, target: {:016X}, index: {})"sv,
						cs.site, cs.target, constructIdx);
				}
				else
				{
					REX::WARN("[Profiler/ESP] DetourCall failed for ConstructObjectList "
						"at site {:016X}"sv, cs.site);
				}
			}
			else
			{
				REX::WARN("[Profiler/ESP] ConstructObjectList call index {} out of range "
					"(found {} calls). Review call-site dump above."sv,
					constructIdx, callSites.size());
			}

			// ---- Step 5: Hook InitAllForms (call-site patch) ----
			// This is a single-param call (this only) after the file loop in CompileFiles.
			// Resolves cross-references and finalizes all loaded forms.

			const std::size_t initFormsIdx = isOG
				? kOG_InitAllFormsIdx : kNG_InitAllFormsIdx;

			if (initFormsIdx < callSites.size())
			{
				const auto& cs = callSites[initFormsIdx];

				*(uintptr_t*)(&OriginalInitAllForms) =
					RELEX::DetourCall(cs.site, (uintptr_t)&HookInitAllForms);

				if (OriginalInitAllForms)
				{
					REX::INFO("[Profiler/ESP] InitAllForms hooked "
						"(site: {:016X}, target: {:016X}, index: {})"sv,
						cs.site, cs.target, initFormsIdx);
				}
				else
				{
					REX::WARN("[Profiler/ESP] DetourCall failed for InitAllForms "
						"at site {:016X}"sv, cs.site);
				}
			}
			else
			{
				REX::WARN("[Profiler/ESP] InitAllForms call index {} out of range "
					"(found {} calls). Review call-site dump above."sv,
					initFormsIdx, callSites.size());
			}
		}

		// ---- Done ----

		m_installed = (OriginalCompileFiles != nullptr);

		REX::INFO("[Profiler/ESP] Installation {} "
			"(CompileFiles: {}, ConstructObjectList: {}, InitAllForms: {})"sv,
			m_installed ? "complete" : "FAILED",
			OriginalCompileFiles ? "OK" : "FAIL",
			OriginalConstructObjectList ? "OK" : "SKIP",
			OriginalInitAllForms ? "OK" : "SKIP");
	}
}

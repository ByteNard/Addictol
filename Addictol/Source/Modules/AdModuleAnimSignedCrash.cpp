#include <Modules/AdModuleAnimSignedCrash.h>
#include <AdUtils.h>
#include <REL/REL.h>
#include <RE/IDs.h>

// Port of SSE Engine Fixes' AnimationLoadSignedCrash.
// Source: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/animation_load_signed_crash.h
//
// Bug: hkbBehaviorGraph::processEventlessGlobalTransitions reads 16-bit event
// IDs from the behavior graph's transition array using `movsx r9d, word ptr [rdi+rax]`.
// The signed read turns event IDs >= 0x8000 into negative ints, and the callee
// uses the value as an array index, crashing on out-of-bounds.
//
// Patch: flip the BF byte to B7 -> instruction becomes `movzx r9d, word ptr [...]`,
// zero-extending the 16-bit value to a non-negative int.

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAnimSignedCrash{ "Fixes"sv, "bAnimSignedCrash"sv, true };

	ModuleAnimSignedCrash::ModuleAnimSignedCrash() :
		Module("Anim Signed Crash", &bFixesAnimSignedCrash)
	{}

	bool ModuleAnimSignedCrash::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAnimSignedCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG (1.10.984) and AE (1.11.191) share Address Library ID 2260478 for
			// hkbBehaviorGraph::processEventlessGlobalTransitions, both with the
			// movsx instruction's BF byte at offset 0x8E.
			// Pre-patch byte at +0x8E: BF (movsx r32, r/m16)
			// Post-patch byte:         B7 (movzx r32, r/m16)
			RELEX::WriteSafe(REL::ID(2260478).address() + 0x8E, { 0xB7 });
		}
		else
		{
			// OG (1.10.163): Address Library ID 919820 for the same function.
			// Compiler interleaved instructions slightly differently, so the
			// movsx's BF byte sits at offset 0x8B instead of 0x8E.
			// Pre-patch byte at +0x8B: BF (movsx r32, r/m16)
			// Post-patch byte:         B7 (movzx r32, r/m16)
			RELEX::WriteSafe(REL::ID(919820).address() + 0x8B, { 0xB7 });
		}

		return true;
	}

	bool ModuleAnimSignedCrash::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAnimSignedCrash::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}

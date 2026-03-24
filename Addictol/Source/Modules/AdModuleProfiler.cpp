#include <Modules/AdModuleProfiler.h>
#include <AdProfilerCore.h>
#include <AdProfilerESP.h>
#include <AdProfilerDLL.h>
#include <AdProfilerMemory.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bProfilerEnabled{ "Profiler"sv, "bProfiler"sv, false };

	ModuleProfiler::ModuleProfiler() :
		Module("Profiler", &bProfilerEnabled, { F4SE::MessagingInterface::kGameDataReady })
	{}

	bool ModuleProfiler::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleProfiler::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto profiler = ProfilerCore::GetSingleton();
		if (!profiler->IsActive())
			profiler->Start();

		// Install ESP/ESM load profiler hooks
		auto espProfiler = ESPProfiler::GetSingleton();
		if (!espProfiler->IsInstalled())
		{
			espProfiler->Install();
			if (espProfiler->IsInstalled())
				REX::INFO("[Profiler] ESP/ESM profiler hooks installed"sv);
		}

		// Capture memory baseline
		auto memProfiler = ProfilerMemory::GetSingleton();
		if (!memProfiler->HasBaseline())
		{
			memProfiler->CaptureBaseline();
			REX::INFO("[Profiler] Memory baseline captured"sv);
		}

		REX::INFO("[Profiler] Module installed, profiling active"sv);
		return true;
	}

	bool ModuleProfiler::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			auto profiler = ProfilerCore::GetSingleton();
			if (profiler->IsActive())
			{
				// Capture final memory snapshot
				ProfilerMemory::GetSingleton()->CaptureSnapshot("GameDataReady"sv);

				profiler->MarkPhase("GameDataReady"sv);
				profiler->GenerateReport();
			}
		}

		return true;
	}

	bool ModuleProfiler::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}

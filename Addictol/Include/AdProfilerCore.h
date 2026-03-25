#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <atomic>
#include <filesystem>

#include <REX\REX\Singleton.h>
#include <REX\REX\TOML.h>
#include <REX\REX\LOG.h>

namespace Addictol
{
	using namespace std::literals;

	// High-resolution scoped timer that records elapsed milliseconds
	class ScopedProfileTimer
	{
		std::chrono::high_resolution_clock::time_point m_start;
		double& m_target;
	public:
		explicit ScopedProfileTimer(double& a_target) noexcept :
			m_start(std::chrono::high_resolution_clock::now()),
			m_target(a_target)
		{}

		~ScopedProfileTimer() noexcept
		{
			auto end = std::chrono::high_resolution_clock::now();
			m_target = std::chrono::duration<double, std::milli>(end - m_start).count();
		}
	};

	// Data structures for profiling results
	struct ESPProfileEntry
	{
		std::string filename;
		std::int32_t loadOrderIndex{ 0 };
		double openMs{ 0.0 };
		double constructMs{ 0.0 };
		double closeMs{ 0.0 };
		double totalMs{ 0.0 };
	};

	struct DLLProfileEntry
	{
		std::string dllName;
		std::string dllPath;
		double queryMs{ 0.0 };
		double loadMs{ 0.0 };
		std::string fileVersion;
	};

	struct ModuleProfileEntry
	{
		std::string moduleName;
		double queryMs{ 0.0 };
		double installMs{ 0.0 };
		bool querySuccess{ false };
		bool installSuccess{ false };
	};

	struct StartupPhase
	{
		std::string name;
		std::chrono::high_resolution_clock::time_point timestamp;
		double elapsedFromStartMs{ 0.0 };
	};

	struct MemorySnapshot
	{
		std::string phaseName;
		std::size_t totalAllocated{ 0 };
		std::size_t totalFreed{ 0 };
		std::size_t peakUsage{ 0 };
		std::size_t allocationCount{ 0 };
	};

	struct BA2ProfileEntry
	{
		std::string archiveName;
		double decompressMs{ 0.0 };
		std::size_t compressedSize{ 0 };
		std::size_t uncompressedSize{ 0 };
		double throughputMBps{ 0.0 };
	};

	// Central profiler data collector
	class ProfilerCore :
		public REX::Singleton<ProfilerCore>
	{
		std::chrono::high_resolution_clock::time_point m_startTime;
		bool m_active{ false };

		// ESP/ESM data
		std::vector<ESPProfileEntry> m_espEntries;
		double m_totalCompileMs{ 0.0 };
		double m_initAllFormsMs{ 0.0 };
		std::mutex m_espMutex;

		// DLL data
		std::vector<DLLProfileEntry> m_dllEntries;
		std::mutex m_dllMutex;

		// Module timing data
		std::vector<ModuleProfileEntry> m_moduleEntries;
		std::mutex m_moduleMutex;

		// Startup timeline
		std::vector<StartupPhase> m_startupPhases;
		std::mutex m_startupMutex;

		// Memory snapshots
		std::vector<MemorySnapshot> m_memorySnapshots;
		std::mutex m_memoryMutex;

		// BA2 data
		std::vector<BA2ProfileEntry> m_ba2Entries;
		std::mutex m_ba2Mutex;

		ProfilerCore(const ProfilerCore&) = delete;
		ProfilerCore& operator=(const ProfilerCore&) = delete;
	public:
		ProfilerCore() = default;
		virtual ~ProfilerCore() = default;

		void Start() noexcept;
		[[nodiscard]] bool IsActive() const noexcept { return m_active; }
		[[nodiscard]] static bool IsEnabledInConfig() noexcept;
		[[nodiscard]] static bool IsESPEnabled() noexcept;
		[[nodiscard]] static bool IsDLLEnabled() noexcept;
		[[nodiscard]] static bool IsModuleProfilingEnabled() noexcept;
		[[nodiscard]] static bool IsStartupTimelineEnabled() noexcept;
		[[nodiscard]] static bool IsMemoryTrackingEnabled() noexcept;
		[[nodiscard]] static bool IsBA2TimingEnabled() noexcept;

		// Startup timeline
		void MarkPhase(std::string_view a_name) noexcept;

		// ESP/ESM profiling
		void AddESPEntry(ESPProfileEntry&& a_entry) noexcept;
		void SetTotalCompileTime(double a_ms) noexcept { m_totalCompileMs = a_ms; }
		void SetInitAllFormsTime(double a_ms) noexcept { m_initAllFormsMs = a_ms; }

		// DLL profiling
		void AddDLLEntry(DLLProfileEntry&& a_entry) noexcept;

		// Module profiling
		void AddModuleEntry(ModuleProfileEntry&& a_entry) noexcept;

		// Memory tracking
		void AddMemorySnapshot(MemorySnapshot&& a_snapshot) noexcept;

		// BA2 timing
		void AddBA2Entry(BA2ProfileEntry&& a_entry) noexcept;

		// Report generation
		void GenerateReport() noexcept;
		void ExportCSV() noexcept;

		// Accessors
		[[nodiscard]] const std::vector<ESPProfileEntry>& GetESPEntries() const noexcept { return m_espEntries; }
		[[nodiscard]] const std::vector<DLLProfileEntry>& GetDLLEntries() const noexcept { return m_dllEntries; }
		[[nodiscard]] const std::vector<ModuleProfileEntry>& GetModuleEntries() const noexcept { return m_moduleEntries; }
		[[nodiscard]] const std::vector<StartupPhase>& GetStartupPhases() const noexcept { return m_startupPhases; }
		[[nodiscard]] const std::vector<BA2ProfileEntry>& GetBA2Entries() const noexcept { return m_ba2Entries; }

	private:
		[[nodiscard]] std::string GetOutputDir() const noexcept;
		void LogESPReport() noexcept;
		void LogDLLReport() noexcept;
		void LogModuleReport() noexcept;
		void LogStartupTimeline() noexcept;
		void LogMemoryReport() noexcept;
		void LogBA2Report() noexcept;
	};
}

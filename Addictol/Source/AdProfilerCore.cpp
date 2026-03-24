#include <AdProfilerCore.h>
#include <AdUtils.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <ctime>

namespace Addictol
{
	// TOML config options for the profiler
	static REX::TOML::Bool<> bProfiler{ "Profiler"sv, "bProfiler"sv, false };
	static REX::TOML::Bool<> bESPProfiler{ "Profiler"sv, "bESPProfiler"sv, false };
	static REX::TOML::Bool<> bDLLProfiler{ "Profiler"sv, "bDLLProfiler"sv, true };
	static REX::TOML::Bool<> bModuleProfiler{ "Profiler"sv, "bModuleProfiler"sv, true };
	static REX::TOML::Bool<> bStartupTimeline{ "Profiler"sv, "bStartupTimeline"sv, true };
	static REX::TOML::Bool<> bMemoryTracking{ "Profiler"sv, "bMemoryTracking"sv, true };
	static REX::TOML::Bool<> bBA2Timing{ "Profiler"sv, "bBA2Timing"sv, true };
	static REX::TOML::Bool<> bCSVExport{ "Profiler"sv, "bCSVExport"sv, true };

	void ProfilerCore::Start() noexcept
	{
		m_startTime = std::chrono::high_resolution_clock::now();
		m_active = true;
		MarkPhase("ProfilerStart"sv);
		REX::INFO("[Profiler] Performance profiler started"sv);
	}

	bool ProfilerCore::IsEnabledInConfig() noexcept
	{
		return bProfiler.GetValue();
	}

	bool ProfilerCore::IsESPEnabled() noexcept
	{
		return bESPProfiler.GetValue();
	}

	bool ProfilerCore::IsDLLEnabled() noexcept
	{
		return bDLLProfiler.GetValue();
	}

	bool ProfilerCore::IsModuleProfilingEnabled() noexcept
	{
		return bModuleProfiler.GetValue();
	}

	bool ProfilerCore::IsStartupTimelineEnabled() noexcept
	{
		return bStartupTimeline.GetValue();
	}

	bool ProfilerCore::IsMemoryTrackingEnabled() noexcept
	{
		return bMemoryTracking.GetValue();
	}

	bool ProfilerCore::IsBA2TimingEnabled() noexcept
	{
		return bBA2Timing.GetValue();
	}

	void ProfilerCore::MarkPhase(std::string_view a_name) noexcept
	{
		if (!m_active || !bStartupTimeline.GetValue())
			return;

		auto now = std::chrono::high_resolution_clock::now();
		double elapsed = std::chrono::duration<double, std::milli>(now - m_startTime).count();

		std::lock_guard lock(m_startupMutex);
		m_startupPhases.push_back({ std::string(a_name), now, elapsed });
	}

	void ProfilerCore::AddESPEntry(ESPProfileEntry&& a_entry) noexcept
	{
		if (!m_active)
			return;

		std::lock_guard lock(m_espMutex);
		m_espEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddDLLEntry(DLLProfileEntry&& a_entry) noexcept
	{
		if (!m_active)
			return;

		std::lock_guard lock(m_dllMutex);
		m_dllEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddModuleEntry(ModuleProfileEntry&& a_entry) noexcept
	{
		if (!m_active)
			return;

		std::lock_guard lock(m_moduleMutex);
		m_moduleEntries.push_back(std::move(a_entry));
	}

	void ProfilerCore::AddMemorySnapshot(MemorySnapshot&& a_snapshot) noexcept
	{
		if (!m_active)
			return;

		std::lock_guard lock(m_memoryMutex);
		m_memorySnapshots.push_back(std::move(a_snapshot));
	}

	void ProfilerCore::AddBA2Entry(BA2ProfileEntry&& a_entry) noexcept
	{
		if (!m_active)
			return;

		std::lock_guard lock(m_ba2Mutex);
		m_ba2Entries.push_back(std::move(a_entry));
	}

	// -- Report Generation --

	std::string ProfilerCore::GetOutputDir() const noexcept
	{
		std::string dir = AdGetRuntimeDirectory() + "Data\\F4SE\\Plugins\\Addictol\\Profiler\\";

		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		return dir;
	}

	void ProfilerCore::LogESPReport() noexcept
	{
		if (m_espEntries.empty())
			return;

		REX::INFO("[Profiler] ===== ESP/ESM Load Time Report ====="sv);
		REX::INFO("[Profiler]   Total CompileFiles: {:.1f} ms ({:.2f} s)"sv, m_totalCompileMs, m_totalCompileMs / 1000.0);
		REX::INFO("[Profiler]   InitAllForms: {:.1f} ms"sv, m_initAllFormsMs);
		REX::INFO("[Profiler]   Files loaded: {}"sv, m_espEntries.size());

		// Sort by construct time descending for the report
		auto sorted = m_espEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.constructMs > b.constructMs; });

		REX::INFO("[Profiler]   --- Top files by load time ---"sv);
		std::size_t reportCount = std::min(sorted.size(), static_cast<std::size_t>(20));
		for (std::size_t i = 0; i < reportCount; ++i)
		{
			const auto& e = sorted[i];
			REX::INFO("[Profiler]   [{:3d}] {:40s} {:8.1f} ms (open: {:.1f}, construct: {:.1f}, close: {:.1f})"sv,
				e.loadOrderIndex, e.filename, e.totalMs, e.openMs, e.constructMs, e.closeMs);
		}
	}

	void ProfilerCore::LogDLLReport() noexcept
	{
		if (m_dllEntries.empty())
			return;

		REX::INFO("[Profiler] ===== F4SE Plugin DLL Load Time Report ====="sv);

		double totalLoad = 0.0, totalQuery = 0.0;
		for (const auto& e : m_dllEntries)
		{
			totalLoad += e.loadMs;
			totalQuery += e.queryMs;
		}

		REX::INFO("[Profiler]   Plugins loaded: {}"sv, m_dllEntries.size());
		REX::INFO("[Profiler]   Total Query time: {:.1f} ms"sv, totalQuery);
		REX::INFO("[Profiler]   Total Load time: {:.1f} ms"sv, totalLoad);

		// Sort by load time descending
		auto sorted = m_dllEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.loadMs > b.loadMs; });

		for (const auto& e : sorted)
		{
			REX::INFO("[Profiler]   {:40s} Load: {:8.1f} ms  Query: {:6.1f} ms  Ver: {}"sv,
				e.dllName, e.loadMs, e.queryMs,
				e.fileVersion.empty() ? "(none)"sv : std::string_view(e.fileVersion));
		}
	}

	void ProfilerCore::LogModuleReport() noexcept
	{
		if (m_moduleEntries.empty())
			return;

		REX::INFO("[Profiler] ===== Addictol Module Init Time Report ====="sv);

		double totalQuery = 0.0, totalInstall = 0.0;
		for (const auto& e : m_moduleEntries)
		{
			totalQuery += e.queryMs;
			totalInstall += e.installMs;
		}

		REX::INFO("[Profiler]   Modules: {}"sv, m_moduleEntries.size());
		REX::INFO("[Profiler]   Total Query time: {:.3f} ms"sv, totalQuery);
		REX::INFO("[Profiler]   Total Install time: {:.3f} ms"sv, totalInstall);

		// Sort by install time descending
		auto sorted = m_moduleEntries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.installMs > b.installMs; });

		for (const auto& e : sorted)
		{
			REX::INFO("[Profiler]   {:30s} Query: {:8.3f} ms ({})  Install: {:8.3f} ms ({})"sv,
				e.moduleName, e.queryMs, e.querySuccess ? "ok" : "FAIL",
				e.installMs, e.installSuccess ? "ok" : "FAIL");
		}
	}

	void ProfilerCore::LogStartupTimeline() noexcept
	{
		if (m_startupPhases.empty())
			return;

		REX::INFO("[Profiler] ===== Startup Timeline ====="sv);
		for (std::size_t i = 0; i < m_startupPhases.size(); ++i)
		{
			const auto& phase = m_startupPhases[i];
			double deltaMs = 0.0;
			if (i > 0)
				deltaMs = phase.elapsedFromStartMs - m_startupPhases[i - 1].elapsedFromStartMs;

			REX::INFO("[Profiler]   [{:8.1f} ms] {:30s} (delta: {:8.1f} ms)"sv,
				phase.elapsedFromStartMs, phase.name, deltaMs);
		}
	}

	void ProfilerCore::LogMemoryReport() noexcept
	{
		if (m_memorySnapshots.empty())
			return;

		REX::INFO("[Profiler] ===== Memory Usage Report ====="sv);
		for (const auto& snap : m_memorySnapshots)
		{
			REX::INFO("[Profiler]   {:30s} Alloc: {:>10} bytes  Freed: {:>10} bytes  Peak: {:>10} bytes  Count: {}"sv,
				snap.phaseName, snap.totalAllocated, snap.totalFreed, snap.peakUsage, snap.allocationCount);
		}
	}

	void ProfilerCore::LogBA2Report() noexcept
	{
		if (m_ba2Entries.empty())
			return;

		REX::INFO("[Profiler] ===== BA2 Decompression Report ====="sv);

		double totalMs = 0.0;
		std::size_t totalCompressed = 0, totalUncompressed = 0;
		for (const auto& e : m_ba2Entries)
		{
			totalMs += e.decompressMs;
			totalCompressed += e.compressedSize;
			totalUncompressed += e.uncompressedSize;
		}

		double totalMBps = totalMs > 0.0 ? (static_cast<double>(totalUncompressed) / (1024.0 * 1024.0)) / (totalMs / 1000.0) : 0.0;

		REX::INFO("[Profiler]   Archives: {}"sv, m_ba2Entries.size());
		REX::INFO("[Profiler]   Total decompress time: {:.1f} ms"sv, totalMs);
		REX::INFO("[Profiler]   Total compressed: {:.2f} MB"sv, static_cast<double>(totalCompressed) / (1024.0 * 1024.0));
		REX::INFO("[Profiler]   Total uncompressed: {:.2f} MB"sv, static_cast<double>(totalUncompressed) / (1024.0 * 1024.0));
		REX::INFO("[Profiler]   Average throughput: {:.1f} MB/s"sv, totalMBps);

		// Sort by decompress time descending
		auto sorted = m_ba2Entries;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.decompressMs > b.decompressMs; });

		std::size_t reportCount = std::min(sorted.size(), static_cast<std::size_t>(20));
		for (std::size_t i = 0; i < reportCount; ++i)
		{
			const auto& e = sorted[i];
			REX::INFO("[Profiler]   {:40s} {:8.1f} ms ({:.1f} MB/s)"sv,
				e.archiveName, e.decompressMs, e.throughputMBps);
		}
	}

	void ProfilerCore::GenerateReport() noexcept
	{
		if (!m_active)
			return;

		MarkPhase("ReportGeneration"sv);

		REX::INFO("[Profiler] ========================================"sv);
		REX::INFO("[Profiler]   ADDICTOL PERFORMANCE PROFILER REPORT"sv);
		REX::INFO("[Profiler] ========================================"sv);

		if (bStartupTimeline.GetValue())
			LogStartupTimeline();
		if (bDLLProfiler.GetValue())
			LogDLLReport();
		if (bESPProfiler.GetValue())
			LogESPReport();
		if (bModuleProfiler.GetValue())
			LogModuleReport();
		if (bMemoryTracking.GetValue())
			LogMemoryReport();
		if (bBA2Timing.GetValue())
			LogBA2Report();

		if (bCSVExport.GetValue())
			ExportCSV();

		REX::INFO("[Profiler] ========================================"sv);
		REX::INFO("[Profiler]   END OF PROFILER REPORT"sv);
		REX::INFO("[Profiler] ========================================"sv);
	}

	void ProfilerCore::ExportCSV() noexcept
	{
		auto dir = GetOutputDir();
		if (dir.empty())
			return;

		// Timestamp for unique filenames
		auto now = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &now);

		char timeBuf[64];
		std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tm);

		// ESP/ESM CSV
		if (!m_espEntries.empty())
		{
			std::string path = dir + "esp_load_times_" + timeBuf + ".csv";
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "LoadOrder,Filename,OpenMs,ConstructMs,CloseMs,TotalMs\n";
				for (const auto& e : m_espEntries)
				{
					file << e.loadOrderIndex << ","
						<< "\"" << e.filename << "\","
						<< std::fixed << std::setprecision(1)
						<< e.openMs << ","
						<< e.constructMs << ","
						<< e.closeMs << ","
						<< e.totalMs << "\n";
				}
				REX::INFO("[Profiler] ESP CSV exported: {}"sv, path);
			}
		}

		// DLL CSV
		if (!m_dllEntries.empty())
		{
			std::string path = dir + "dll_load_times_" + timeBuf + ".csv";
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "DLLName,QueryMs,LoadMs,FileVersion,DLLPath\n";
				for (const auto& e : m_dllEntries)
				{
					file << "\"" << e.dllName << "\","
						<< std::fixed << std::setprecision(1)
						<< e.queryMs << ","
						<< e.loadMs << ","
						<< "\"" << e.fileVersion << "\","
						<< "\"" << e.dllPath << "\"\n";
				}
				REX::INFO("[Profiler] DLL CSV exported: {}"sv, path);
			}
		}

		// Module CSV
		if (!m_moduleEntries.empty())
		{
			std::string path = dir + "module_times_" + timeBuf + ".csv";
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "ModuleName,QueryMs,QuerySuccess,InstallMs,InstallSuccess\n";
				for (const auto& e : m_moduleEntries)
				{
					file << "\"" << e.moduleName << "\","
						<< std::fixed << std::setprecision(3)
						<< e.queryMs << ","
						<< (e.querySuccess ? "true" : "false") << ","
						<< e.installMs << ","
						<< (e.installSuccess ? "true" : "false") << "\n";
				}
				REX::INFO("[Profiler] Module CSV exported: {}"sv, path);
			}
		}

		// BA2 CSV
		if (!m_ba2Entries.empty())
		{
			std::string path = dir + "ba2_decompress_times_" + timeBuf + ".csv";
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "ArchiveName,DecompressMs,CompressedBytes,UncompressedBytes,ThroughputMBps\n";
				for (const auto& e : m_ba2Entries)
				{
					file << "\"" << e.archiveName << "\","
						<< std::fixed << std::setprecision(1)
						<< e.decompressMs << ","
						<< e.compressedSize << ","
						<< e.uncompressedSize << ","
						<< std::setprecision(1) << e.throughputMBps << "\n";
				}
				REX::INFO("[Profiler] BA2 CSV exported: {}"sv, path);
			}
		}

		// Startup Timeline CSV
		if (!m_startupPhases.empty())
		{
			std::string path = dir + "startup_timeline_" + timeBuf + ".csv";
			std::ofstream file(path);
			if (file.is_open())
			{
				file << "Phase,ElapsedMs,DeltaMs\n";
				for (std::size_t i = 0; i < m_startupPhases.size(); ++i)
				{
					const auto& phase = m_startupPhases[i];
					double delta = (i > 0) ? phase.elapsedFromStartMs - m_startupPhases[i - 1].elapsedFromStartMs : 0.0;
					file << "\"" << phase.name << "\","
						<< std::fixed << std::setprecision(1)
						<< phase.elapsedFromStartMs << ","
						<< delta << "\n";
				}
				REX::INFO("[Profiler] Startup timeline CSV exported: {}"sv, path);
			}
		}
	}
}

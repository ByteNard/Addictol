#pragma once

#include <string_view>

#include <REX\REX\Singleton.h>
#include <REX\REX\LOG.h>

namespace Addictol
{
	using namespace std::literals;

	// Per-phase process memory tracker using Windows Process Memory APIs.
	// Captures WorkingSetSize, PagefileUsage, and PeakWorkingSetSize
	// from GetProcessMemoryInfo() at each loading phase, computing
	// deltas from a recorded baseline.
	//
	// MemorySnapshot field mapping (process-level, not per-allocation):
	//   totalAllocated  = WorkingSetSize     (current physical memory)
	//   totalFreed      = PagefileUsage      (committed virtual memory)
	//   peakUsage       = PeakWorkingSetSize (peak physical memory)
	//   allocationCount = WorkingSetSize delta from baseline (net growth)
	class ProfilerMemory :
		public REX::Singleton<ProfilerMemory>
	{
		std::size_t m_baselineWorkingSet{ 0 };
		std::size_t m_baselinePagefile{ 0 };
		std::size_t m_baselinePeak{ 0 };
		bool m_baselineCaptured{ false };

		ProfilerMemory(const ProfilerMemory&) = delete;
		ProfilerMemory& operator=(const ProfilerMemory&) = delete;
	public:
		ProfilerMemory() = default;
		virtual ~ProfilerMemory() = default;

		// Record the current process memory state as the baseline.
		// Also submits a "Baseline" snapshot to ProfilerCore.
		void CaptureBaseline() noexcept;

		// Capture a snapshot at the named phase, computing deltas
		// from baseline, and submit it to ProfilerCore.
		void CaptureSnapshot(std::string_view a_phaseName) noexcept;

		// Accessors
		[[nodiscard]] bool HasBaseline() const noexcept { return m_baselineCaptured; }
		[[nodiscard]] std::size_t GetBaselineWorkingSet() const noexcept { return m_baselineWorkingSet; }
	};
}

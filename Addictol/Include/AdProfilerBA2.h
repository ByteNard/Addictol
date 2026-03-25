#pragma once

#include <atomic>

#include <AdProfilerCore.h>

namespace Addictol
{
	using namespace std::literals;

	// Thread-safe BA2 decompression timing recorder.
	// Records individual chunk-level decompression calls from the libdeflate hook
	// and forwards them to ProfilerCore for aggregation and reporting.
	class ProfilerBA2 :
		public REX::Singleton<ProfilerBA2>
	{
		std::atomic<std::uint64_t> m_chunkCounter{ 0 };

	public:
		// Record a completed decompression operation with pre-measured timing.
		// Thread-safe: called from multiple BA2 decompression worker threads.
		//   a_compressedSize   - actual bytes consumed from the compressed stream
		//   a_uncompressedSize - actual bytes produced into the output buffer
		//   a_elapsedMs        - wall-clock milliseconds for the decompression call
		void RecordDecompression(std::size_t a_compressedSize,
			std::size_t a_uncompressedSize,
			double a_elapsedMs) noexcept;
	};
}

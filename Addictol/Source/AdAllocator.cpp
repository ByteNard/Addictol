#include <AdAssert.h>
#include <AdAllocator.h>
#include <REX\REX\TOML.h>
#include <windows.h>

namespace Addictol
{
	// Use to convert bytes to KB
	constexpr static auto AD_DIV = 1024;

	void* ICheckerPointer::CheckPtr(void* lpBlock, size_t nSize) const noexcept
	{
		if (!lpBlock)
		{
			static MEMORYSTATUSEX statex = { 0 };
			statex.dwLength = sizeof(MEMORYSTATUSEX);
			if (!GlobalMemoryStatusEx(&statex))
				return lpBlock;

			//  https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex
			//  Sample output:
			//  OUT OF MEMORY. A memory allocation failed.
			//  
			//  Requested chunk size: 123456 bytes.
			//  There is       51 percent of memory in use.
			//  There are 2029968 total KB of physical memory.
			//  There are  987388 free  KB of physical memory.
			//  There are 3884620 total KB of paging file.
			//  There are 2799776 free  KB of paging file.
			//  There are 2097024 total KB of virtual memory.
			//  There are 2084876 free  KB of virtual memory.
			//  There are       0 free  KB of extended memory.
			AdAssertWithFormattedMessage(lpBlock,
				"OUT OF MEMORY. A memory allocation failed.\n\n"
				"Requested chunk size: %llu bytes.\n"
				"There is  %*ld percent of memory in use.\n"
				"There are %*I64d total KB of physical memory.\n"
				"There are %*I64d free  KB of physical memory.\n"
				"There are %*I64d total KB of paging file.\n"
				"There are %*I64d free  KB of paging file.\n"
				"There are %*I64d total KB of virtual memory.\n"
				"There are %*I64d free  KB of virtual memory.\n"
				"There are %*I64d free  KB of extended memory.\n",
				nSize, statex.dwMemoryLoad, 
				statex.ullTotalPhys / AD_DIV, statex.ullAvailPhys / AD_DIV,
				statex.ullTotalPageFile / AD_DIV, statex.ullAvailPageFile / AD_DIV,
				statex.ullTotalVirtual / AD_DIV, statex.ullAvailVirtual / AD_DIV,
				statex.ullAvailExtendedVirtual / AD_DIV);
		}

		return lpBlock;
	}
}
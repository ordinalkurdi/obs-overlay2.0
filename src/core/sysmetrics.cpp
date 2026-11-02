#include "sysmetrics.hpp"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#else
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace ovl {

SysMetrics sample_system_metrics()
{
	SysMetrics m;
#if defined(_WIN32)
	static ULONGLONG last_idle = 0, last_total = 0;
	FILETIME idle, kernel, user;
	if (GetSystemTimes(&idle, &kernel, &user)) {
		auto u64 = [](const FILETIME &f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; };
		ULONGLONG i = u64(idle), t = u64(kernel) + u64(user); // kernel time includes idle time
		if (last_total && t > last_total) m.cpu = 100.0 * (1.0 - (double)(i - last_idle) / (double)(t - last_total));
		last_idle = i; last_total = t;
	}
	MEMORYSTATUSEX ms = {};
	ms.dwLength = sizeof ms;
	if (GlobalMemoryStatusEx(&ms)) m.mem = (double)ms.dwMemoryLoad;
#elif defined(__APPLE__)
	static natural_t last_idle = 0, last_total = 0;
	host_cpu_load_info_data_t info;
	mach_msg_type_number_t cnt = HOST_CPU_LOAD_INFO_COUNT;
	if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &cnt) == KERN_SUCCESS) {
		natural_t idle = info.cpu_ticks[CPU_STATE_IDLE], total = 0;
		for (int k = 0; k < CPU_STATE_MAX; ++k) total += info.cpu_ticks[k];
		if (last_total && total > last_total) m.cpu = 100.0 * (1.0 - (double)(idle - last_idle) / (double)(total - last_total));
		last_idle = idle; last_total = total;
	}
	uint64_t mem = 0; size_t len = sizeof mem;
	vm_statistics64_data_t vm; mach_msg_type_number_t vcnt = HOST_VM_INFO64_COUNT;
	if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 && mem &&
	    host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &vcnt) == KERN_SUCCESS) {
		uint64_t avail = ((uint64_t)vm.free_count + vm.inactive_count) * (uint64_t)vm_page_size;
		m.mem = 100.0 * (1.0 - (double)avail / (double)mem);
	}
#else
	static unsigned long long last_idle = 0, last_total = 0;
	std::ifstream st("/proc/stat");
	std::string tag;
	unsigned long long v[8] = {};
	if (st >> tag && tag == "cpu") {
		for (auto &x : v) st >> x;
		unsigned long long idle = v[3] + v[4], total = 0;
		for (auto x : v) total += x;
		if (last_total && total > last_total) m.cpu = 100.0 * (1.0 - (double)(idle - last_idle) / (double)(total - last_total));
		last_idle = idle; last_total = total;
	}
	std::ifstream mi("/proc/meminfo");
	std::string key; unsigned long long val = 0, total = 0, avail = 0; std::string unit;
	while (mi >> key >> val >> unit) {
		if (key == "MemTotal:") total = val;
		else if (key == "MemAvailable:") avail = val;
	}
	if (total) m.mem = 100.0 * (1.0 - (double)avail / (double)total);
#endif
	return m;
}

} // namespace ovl

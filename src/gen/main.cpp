#if defined(__GNUC__) || defined(__clang__)
# define dmb_noinline __attribute__((noinline))
#elif defined(_MSC_VER)
# define dmb_noinline __declspec(noinline)
#endif

#if defined(_WIN32)
# include <intrin.h>
#else
# include <x86intrin.h>
#endif

using usize = decltype(sizeof 0);
using utime = usize;

dmb_noinline
static utime measure_tsc_overhead(usize cnt)
{
	utime min_measurement = utime(-1);

	do
	{
#if defined(__GNUC__) || defined(__clang__)
		unsigned bfrlo, bfrhi, afrlo, afrhi;

		_mm_mfence();
		asm volatile(
			"xor %%eax, %%eax\n\t"
			"xor %k2, %%ecx\n\t"  // %k2 - the low 32 bits of %2
			"cpuid\n\t"
			"rdtsc\n\t"
			"lfence\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1"
			: "=r" (bfrhi), "=r" (bfrlo), "+r" (cnt)
			:
			: "%rax", "%rdx", "%rbx", "%rcx"
		);

		// benchmark goes here

		asm volatile(
			"xor %%eax, %%eax\n\t"
			"xor %k2, %%ecx\n\t"
			"cpuid\n\t"
			"rdtsc\n\t"
			"lfence\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1"
			: "=r" (afrhi), "=r" (afrlo), "+r" (cnt)
			:
			: "%rax", "%rdx", "%rbx", "%rcx"
		);
		_mm_mfence();

		const utime measurement = (((utime)afrhi << 32) | afrlo) - (((utime)bfrhi << 32) | bfrlo);
#elif _MSC_VER
		int trash[4];

		_mm_mfence();
		__cpuid(trash, 0);
		const unsigned long long bfr = __rdtsc();
		_mm_lfence();

		// benchmark goes here

		__cpuid(trash, 0);
		const unsigned long long afr = __rdtsc();
		_mm_lfence();
		_mm_mfence();

		const utime measurement = afr - bfr;
#endif
		if (measurement < min_measurement)
			min_measurement = measurement;

		cnt--;
	}
	while (cnt > 0);

	return min_measurement;
}

#include <cstdio>

int main(int argc, const char * argv[])
{
	usize cnt = 100000000; // arbitrary
	if (argc > 1)
	{
#if defined(_WIN32)
		::sscanf_s(argv[1], "%zu", &cnt);
#else
		::sscanf(argv[1], "%zu", &cnt);
#endif
	}

	utime tsc_overhead = measure_tsc_overhead(cnt);

#if defined(_WIN32)
	FILE * overhead_file;
	if (::fopen_s(&overhead_file, "overhead.hpp", "wb") == 0)
#else
	FILE * const overhead_file = ::fopen("overhead.hpp", "wb");
	if (overhead_file != nullptr)
#endif
	{
		::fprintf(overhead_file, "// generated by dmbgen\n\nnamespace dmb\n{\n\tstatic constexpr const utime tsc_overhead = %zu;\n}\n", tsc_overhead);
		::fclose(overhead_file);
	}

	return 0;
}

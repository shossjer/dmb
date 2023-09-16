#define dmb_unused(x) static_cast<void>(x)

namespace dmb
{

	using byte = char;
	using ssize = decltype(static_cast<char *>(nullptr) - static_cast<char *>(nullptr));
	using usize = decltype(sizeof 0);
	using utime = usize;
	using stime = ssize;

	using uint64 = unsigned long;
}

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace dmb
{

	inline int perf_event_open(struct perf_event_attr * hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
	{
		long ret;

		ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);

		return (int)ret;
	}

	inline int perf_event_add(int parent, unsigned long config)
	{
		struct perf_event_attr pe{};
		pe.type = PERF_TYPE_HARDWARE;
		pe.size = sizeof(pe);
		pe.config = config;
		pe.exclude_kernel = 1;
		pe.exclude_hv = 1;

		int fd = perf_event_open(&pe, 0, -1, parent, 0);
		if (fd == -1)
		{
			// /proc/sys/kernel/perf_event_paranoid
			// 3 ought to work
			::perror("perf_event_open");
		}
		return fd;
	}
}

#include <x86intrin.h>

namespace dmb
{

	template <typename U>
	struct num
	{

		using value_type = U;

		value_type value;

		operator value_type () const { return value; }

	};

	struct idx : num<usize>
	{
		idx(usize rep) : num<usize>{rep} {}
	};
	struct u64 : num<uint64>
	{
		u64(usize rep) : num<uint64>{rep} {}
	};

	template <typename T>
	struct pos : T
	{
		pos(usize rep) : T(~rep) {}
	};

	template <typename P>
	__attribute__((always_inline)) inline usize nop(P && p)
	{
		usize garbage;

		asm volatile(
		   ""
		   : "=r" (garbage), "+m" (p)
		   :
		   :
		);

		return garbage;
	}

	template <typename F>
	__attribute__((always_inline)) inline auto invoke(F && f, usize index)
		-> decltype(static_cast<F &&>(f)(index, index))
	{
		return static_cast<F &&>(f)(index, index);
	}

	template <typename F>
	__attribute__((always_inline)) inline auto invoke(F && f, usize index)
		-> decltype(static_cast<F &&>(f)(index))
	{
		return static_cast<F &&>(f)(index);
	}

	template <typename F>
	__attribute__((always_inline)) inline auto invoke(F && f, usize index)
		-> decltype(static_cast<F &&>(f)())
	{
		dmb_unused(index);

		return static_cast<F &&>(f)();
	}

	template <typename F>
	__attribute__((noinline)) inline void timeonce(utime * r, int fd, F && f, usize idx)
	{
		unsigned bfrlo, bfrhi, afrlo, afrhi;

		_mm_mfence();
		::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
		asm volatile(
		   "xor %%eax, %%eax\n\t"
		   "xor %k2, %%edx\n\t"  // %k2 - the low 32 bits of %2
		   "cpuid\n\t"
			"rdtsc\n\t"
		   "lfence\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1"
			: "=r" (bfrhi), "=r" (bfrlo), "+r" (idx)
			:
		   : "%rax", "%rdx", "%rbx", "%rcx"
		);

		auto && t = invoke(static_cast<F &&>(f), idx);

		asm volatile(
		   "xor %%eax, %%eax\n\t"
		   "xor %k2, %%edx\n\t"
		   "cpuid\n\t"
			"rdtsc\n\t"
			"lfence\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1\n\t"
		   : "=r" (afrhi), "=r" (afrlo), "+m" (t)
			:
		   : "%rax", "%rdx", "%rbx", "%rcx"
		);
		::ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
		_mm_mfence();

		*r = (((utime)afrhi << 32) | afrlo) - (((utime)bfrhi << 32) | bfrlo);
	}
}

#define __DMB_CNCT__(stem, line) __##stem##_##line##__
#define __DMB_NAME__(stem, line) __DMB_CNCT__(stem, line)
#define __DMB_FUNC__ __DMB_NAME__(DMB_FUNC, __LINE__)
#define __DMB_GLOB__ __DMB_NAME__(DMB_GLOB, __LINE__)

#include <unistd.h>

namespace dmb
{
	struct cstr
	{
		const char * ptr;
		usize cnt;

		template <usize N>
		constexpr explicit cstr(const char (& str)[N])
			: ptr(str)
			, cnt(N - 1)
		{
		}

		constexpr const char * data() const { return ptr; }
		constexpr usize size() const { return cnt; }
	};

	inline void write_time_and_unit(char * buffer, utime val, utime scl = 1)
	{

		//       0.c            0
		//     999.c          999
		//   1.000kc        1'000
		//   9.999kc        9'999
		//  10.000kc       10'000
		//  99.999kc       99'999
		// 100.000kc      100'000
		// 999.999kc      999'999
		// 1000.00kc    1'000'00-
		// 1999.99kc    1'999'99-
		// 2.00000Mc    2'000'00-
		// 9.99999Mc    9'999'99-
		// 10.0000Mc   10'000'0--
		// 99.9999Mc   99'999'9--
		// 100.000Mc  100'000'---
		// 999.999Mc  999'999'---
		// G
		// T
		// P
		// E

		utime exp = 0;
		if (scl > 1)
		{
			const utime tst = val / scl;
			utime scale = 1;
			if (tst < 10) exp = 5, scale = 100000;
			else if (tst < 100) exp = 4, scale = 10000;
			else if (tst < 1000) exp = 3, scale = 1000;
			else if (tst < 10000) exp = 2, scale = 100;
			else if (tst < 100000) exp = 1, scale = 10;
			val = val * scale / scl;
		}

		if (val < 1000000)
		{
			*reinterpret_cast<long *>(buffer) = 0x2020202020202020;

			int ci = 7;
			if (exp == 0) buffer[ci--] = '.';
			buffer[ci--] = '0' + (val % 10);
			val /= 10;
			if (val > 0)
			{
				if (exp == 1) buffer[ci--] = '.';
				buffer[ci--] = '0' + (val % 10);
				val /= 10;
				if (val > 0)
				{
					if (exp == 2) buffer[ci--] = '.';
					buffer[ci--] = '0' + (val % 10);
					val /= 10;
					if (val > 0)
					{
						if (exp == 3) buffer[ci--] = '.';
						buffer[ci--] = '0' + (val % 10);
						val /= 10;
						if (val > 0)
						{
							if (exp == 4) buffer[ci--] = '.';
							buffer[ci--] = '0' + (val % 10);
							val /= 10;
							if (val > 0)
							{
								if (exp == 5) buffer[ci--] = '.';
								buffer[ci--] = '0' + (val % 10);
							}
						}
					}
				}
			}
		}
		else
		{
			buffer[0] = ' ';
			buffer[1] = ' ';
			buffer[2] = ' ';
			buffer[3] = ' ';
			buffer[4] = ' ';
			buffer[5] = ' ';
			buffer[6] = ' ';
			buffer[7] = ' ';
			// buffer[8] = 'c';
		}
	}

	struct impl;

	using func = void (*)(dmb::impl & dump);

	int fds[5];

	struct exec
	{
		cstr unit;
		usize nsamples;

		explicit exec(cstr unit, usize nsamples)
			: unit(unit)
			, nsamples(nsamples)
		{
		}

		template <typename F>
		void operator = (F && foo)
		{
			::write(STDOUT_FILENO, unit.data(), unit.size());

			::write(STDOUT_FILENO, "  1000\n", 7);

			// todo warmup
			// todo unroll 2, 4, 8
			// todo cold cache
			// todo hot cache

			utime res[1000];
			long fd0[1000];
			long fd1[1000];
			long fd2[1000];
			long fd3[1000];
			long fd4[1000];
			res[1] = 0;
			do
			{
				timeonce(res + 0, fds[0], static_cast<F &&>(foo), 0);
				res[1] += res[0];
			}
			while (res[1] < 2000000);
			usize idx = 0;
			do
			{
				::ioctl(fds[0], PERF_EVENT_IOC_RESET, 0);
				::ioctl(fds[1], PERF_EVENT_IOC_RESET, 0);
				::ioctl(fds[2], PERF_EVENT_IOC_RESET, 0);
				::ioctl(fds[3], PERF_EVENT_IOC_RESET, 0);
				::ioctl(fds[4], PERF_EVENT_IOC_RESET, 0);

				timeonce(res + idx, fds[0], static_cast<F &&>(foo), idx);

				::read(fds[0], fd0 + idx, sizeof(long));
				::read(fds[1], fd1 + idx, sizeof(long));
				::read(fds[2], fd2 + idx, sizeof(long));
				::read(fds[3], fd3 + idx, sizeof(long));
				::read(fds[4], fd4 + idx, sizeof(long));

				idx++;
			}
			while (idx < nsamples);

			// todo measured to be 113 with 1000 samples
			for (usize i = 0; i < nsamples; i++)
			{
				res[i] -= 112;
			}

			// todo measured to be 26
			for (usize i = 0; i < nsamples; i++)
			{
				fd0[i] -= 26;
			}

			utime tot = 0;
			for (usize i = 0; i < nsamples; i++)
			{
				tot += res[i];
			}

			utime min = utime(-1);
			for (usize i = 0; i < nsamples; i++)
			{
				if (res[i] < min)
				{
					min = res[i];
				}
			}

			utime sqrsum = 0;
			for (usize i = 0; i < nsamples; i++)
			{
				const stime diff = static_cast<stime>(res[i] - min);
				sqrsum += static_cast<utime>(diff * diff);
			}

			const utime sqr = (sqrsum + nsamples - 1) / nsamples;
			utime minsqrt = 0;
			utime maxsqrt = sqr / 2;
			utime lulsqrt = static_cast<utime>(-1);
			do
			{
				const utime avgsqrt = (maxsqrt + minsqrt) / 2;
				if (lulsqrt == avgsqrt) {
					break;
				}
				lulsqrt = avgsqrt;

				if (avgsqrt * avgsqrt > sqr) {
					maxsqrt = avgsqrt;
				}
				else {
					minsqrt = avgsqrt;
				}
			}
			while (maxsqrt - minsqrt > 0); // note distance 1 could have highter accuracy

			utime max = utime(0);
			for (usize i = 0; i < nsamples; i++)
			{
				if (res[i] > max)
				{
					max = res[i];
				}
			}

			utime mid = (max + min) / 2;
			utime sdv = (maxsqrt + minsqrt) / 2;

			char buffer[] =
				" min: zxxx.yy?c  max: zxxx.yy?c  avg: zxxx.yy?c  mid: zxxx.yy?c  sdv: zxxx.yy?c\n"
				" ins: ????????n cache:????????n cache:????????m  jmp: ????????n  jmp: ????????m\n";
			write_time_and_unit(buffer + 6, min);
			write_time_and_unit(buffer + 6 + 16, max);
			write_time_and_unit(buffer + 6 + 32, tot, nsamples);
			write_time_and_unit(buffer + 6 + 48, mid);
			write_time_and_unit(buffer + 6 + 64, sdv);
			write_time_and_unit(buffer + 86, (utime)fd0[0]);
			write_time_and_unit(buffer + 86 + 16, (utime)fd1[1]);
			write_time_and_unit(buffer + 86 + 32, (utime)fd2[2]);
			write_time_and_unit(buffer + 86 + 48, (utime)fd3[3]);
			write_time_and_unit(buffer + 86 + 64, (utime)fd4[4]);
			::write(STDOUT_FILENO, buffer, sizeof buffer - 1);
		}
	};

	struct impl
	{
		usize nsamples;
		usize nfuncs;
		func funcs[100];

		impl()
			: nfuncs(0)
		{
			struct perf_event_attr pe{};
			pe.type = PERF_TYPE_HARDWARE;
			pe.size = sizeof(pe);
			pe.config = PERF_COUNT_HW_INSTRUCTIONS;
			pe.disabled = 1;
			pe.exclude_kernel = 1;
			pe.exclude_hv = 1;

			fds[0] = perf_event_open(&pe, 0, -1, -1, 0);
			if (fds[0] == -1)
			{
				// /proc/sys/kernel/perf_event_paranoid
				// 3 ought to work
				::perror("perf_event_open");
				::exit(1);
			}

			fds[1] = perf_event_add(fds[0], PERF_COUNT_HW_CACHE_REFERENCES);
			fds[2] = perf_event_add(fds[0], PERF_COUNT_HW_CACHE_MISSES);
			fds[3] = perf_event_add(fds[0], PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
			fds[4] = perf_event_add(fds[0], PERF_COUNT_HW_BRANCH_MISSES);
		}

		static impl & get()
		{
			static impl x;
			return x;
		}

		exec unit(cstr unit)
		{
			return exec(unit, nsamples);
		}

	};

	struct lin_type { explicit lin_type() = default; };
	struct log_type { explicit log_type() = default; };

	static constexpr const lin_type lin{};
	static constexpr const log_type log{};

	struct glob
	{

		explicit glob(func foo, cstr id)
		{
			dmb_unused(id);

			impl & x = impl::get();
			x.funcs[x.nfuncs++] = foo;
		}

	};
}

#define DMB_SAMPLE(id) \
	static void __DMB_FUNC__(dmb::impl & __DMB__); \
	static dmb::glob __DMB_GLOB__(__DMB_FUNC__, dmb::cstr(id)); \
	void __DMB_FUNC__(dmb::impl & __DMB__)

#define DMB_CRUNCH(id)

// rename to sample and remove sample; it is unnecessary; CRUNCH is
// all we need
#define DMB_CASE(desc, ...) \
	__DMB__.unit(dmb::cstr(desc), ##__VA_ARGS__) = [&]

#define DMB_REPEAT __DMB__.exec = [&]

// todo --fast, only run new units; enabled by default
// todo --redo

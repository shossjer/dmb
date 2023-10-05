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
#include <cmath>
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

#include <cpuid.h> // todo remove
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

	struct cpu_info
	{
		utime tsc_frequency;
	};

	inline void write_time_and_unit(const cpu_info & info, char * buffer, utime val)
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

		static const utime exp_table[] =
		{
			1,
			10,
			100,
			1000,
			10000,
			100000,
			1000000,
			10000000,
			100000000,
			1000000000,
			10000000000,
			100000000000,
			1000000000000,
			10000000000000,
			100000000000000,
			1000000000000000,
			10000000000000000,
			100000000000000000,
			1000000000000000000,
		};

		const int exp = static_cast<int>(::log10(static_cast<double>(1000000 * info.tsc_frequency / val - 1))); // todo fast log10
		const utime scl = exp_table[exp];
		const utime num = val * scl / info.tsc_frequency;

		static const char unit_table[] =
		{
			's',
			'm',
			'u', // todo µ
			'n',
			'p',
		};

		const int magnitude = exp / 3;
		const char unit = unit_table[magnitude - 1];
		const int split = 3 - (exp - magnitude * 3);

		val = num;

		*reinterpret_cast<long *>(buffer) = 0x2020202020202020;

		buffer[7] = unit;

		int ci = 6;
		if (split == ci) buffer[ci--] = '.';
		buffer[ci--] = '0' + (val % 10);
		val /= 10;
		if (val > 0)
		{
			if (split == ci) buffer[ci--] = '.';
			buffer[ci--] = '0' + (val % 10);
			val /= 10;
			if (val > 0)
			{
				if (split == ci) buffer[ci--] = '.';
				buffer[ci--] = '0' + (val % 10);
				val /= 10;
				if (val > 0)
				{
					if (split == ci) buffer[ci--] = '.';
					buffer[ci--] = '0' + (val % 10);
					val /= 10;
					if (val > 0)
					{
						if (split == ci) buffer[ci--] = '.';
						buffer[ci--] = '0' + (val % 10);
						val /= 10;
						if (val > 0)
						{
							if (split == ci) buffer[ci--] = '.';
							buffer[ci--] = '0' + (val % 10);
						}
					}
				}
			}
		}
	}

	inline void write_timeless_number(char * buffer, utime val, utime scl = 1)
	{
		int shift = 0;
		if (scl == 10) shift = 1;
		if (scl == 100) shift = 2;
		if (scl == 1000) shift = 3;
		if (scl == 10000) shift = 4;
		if (scl == 100000) shift = 5;
		if (scl == 1000000) shift = 6;

		static const char unit_table[] =
		{
			' ', 'K', 'M', 'G', 'T', 'P', 'E',
		};

		// todo what is this logic?
		char sillybuffer[20 + 1];
		const int count = ::sprintf(sillybuffer, "%zu", val); // digits in val
		const char unit = unit_table[(count - 1 - shift) / 3];
		const int split = shift != 0 ? 6 - shift : count < 4 ? 7 : count < 7 ? 3 : (count - 1) % 3 + 1;

		while (val >= 1000000)
		{
			val /= 10;
		}

		*reinterpret_cast<long *>(buffer) = 0x2020202020202020;

		buffer[7] = unit;

		int ci = 6;
		if (split == ci) buffer[ci--] = '.';
		buffer[ci--] = '0' + (val % 10);
		val /= 10;
		if (val > 0)
		{
			if (split == ci) buffer[ci--] = '.';
			buffer[ci--] = '0' + (val % 10);
			val /= 10;
			if (val > 0)
			{
				if (split == ci) buffer[ci--] = '.';
				buffer[ci--] = '0' + (val % 10);
				val /= 10;
				if (val > 0)
				{
					if (split == ci) buffer[ci--] = '.';
					buffer[ci--] = '0' + (val % 10);
					val /= 10;
					if (val > 0)
					{
						if (split == ci) buffer[ci--] = '.';
						buffer[ci--] = '0' + (val % 10);
						val /= 10;
						if (val > 0)
						{
							if (split == ci) buffer[ci--] = '.';
							buffer[ci--] = '0' + (val % 10);
						}
					}
				}
			}
		}
	}

	struct impl;

	using func = void (*)(dmb::impl & dump);

	int fds[5];

	struct exec
	{
		const cpu_info & info;

		cstr unit;
		usize nsamples;

		explicit exec(const cpu_info & info, cstr unit, usize nsamples)
			: info(info)
			, unit(unit)
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

			//

			utime min = utime(-1);
			for (usize i = 0; i < nsamples; i++)
			{
				if (res[i] < min)
				{
					min = res[i];
				}
			}

			utime max = 0;
			for (usize i = 0; i < nsamples; i++)
			{
				if (res[i] > max)
				{
					max = res[i];
				}
			}

			//

			float log_median = 0.f;
			for (usize i = 0; i < nsamples; i++)
			{
				log_median += ::logf(static_cast<float>(res[i]));
			}
			log_median /= static_cast<float>(nsamples);

			float log_standard_deviation = 0.f;
			for (usize i = 0; i < nsamples; i++)
			{
				const float diff = ::logf(static_cast<float>(res[i])) - log_median;
				log_standard_deviation += diff * diff;
			}
			log_standard_deviation /= static_cast<float>(nsamples);
			log_standard_deviation = ::sqrtf(log_standard_deviation);

			// this table was shamelessly borrowed from Wikipedia
			//
			// https://en.wikipedia.org/wiki/Standard_normal_table
			//
			// todo generate a more detailed table
			static const float table[41 * 10] = {
				0.00002f, 0.00002f, 0.00002f, 0.00002f, 0.00003f, 0.00003f, 0.00003f, 0.00003f, 0.00003f, 0.00003f,
				0.00003f, 0.00003f, 0.00004f, 0.00004f, 0.00004f, 0.00004f, 0.00004f, 0.00004f, 0.00005f, 0.00005f,
				0.00005f, 0.00005f, 0.00005f, 0.00006f, 0.00006f, 0.00006f, 0.00006f, 0.00007f, 0.00007f, 0.00007f,
				0.00008f, 0.00008f, 0.00008f, 0.00008f, 0.00009f, 0.00009f, 0.00010f, 0.00010f, 0.00010f, 0.00011f,
				0.00011f, 0.00012f, 0.00012f, 0.00013f, 0.00013f, 0.00014f, 0.00014f, 0.00015f, 0.00015f, 0.00016f,
				0.00017f, 0.00017f, 0.00018f, 0.00019f, 0.00019f, 0.00020f, 0.00021f, 0.00022f, 0.00022f, 0.00023f,
				0.00024f, 0.00025f, 0.00026f, 0.00027f, 0.00028f, 0.00029f, 0.00030f, 0.00031f, 0.00032f, 0.00034f,
				0.00035f, 0.00036f, 0.00038f, 0.00039f, 0.00040f, 0.00042f, 0.00043f, 0.00045f, 0.00047f, 0.00048f,
				0.00050f, 0.00052f, 0.00054f, 0.00056f, 0.00058f, 0.00060f, 0.00062f, 0.00064f, 0.00066f, 0.00069f,
				0.00071f, 0.00074f, 0.00076f, 0.00079f, 0.00082f, 0.00084f, 0.00087f, 0.00090f, 0.00094f, 0.00097f,
				0.00100f, 0.00104f, 0.00107f, 0.00111f, 0.00114f, 0.00118f, 0.00122f, 0.00126f, 0.00131f, 0.00135f,
				0.00139f, 0.00144f, 0.00149f, 0.00154f, 0.00159f, 0.00164f, 0.00169f, 0.00175f, 0.00181f, 0.00187f,
				0.00193f, 0.00199f, 0.00205f, 0.00212f, 0.00219f, 0.00226f, 0.00233f, 0.00240f, 0.00248f, 0.00256f,
				0.00264f, 0.00272f, 0.00280f, 0.00289f, 0.00298f, 0.00307f, 0.00317f, 0.00326f, 0.00336f, 0.00347f,
				0.00357f, 0.00368f, 0.00379f, 0.00391f, 0.00402f, 0.00415f, 0.00427f, 0.00440f, 0.00453f, 0.00466f,
				0.00480f, 0.00494f, 0.00508f, 0.00523f, 0.00539f, 0.00554f, 0.00570f, 0.00587f, 0.00604f, 0.00621f,
				0.00639f, 0.00657f, 0.00676f, 0.00695f, 0.00714f, 0.00734f, 0.00755f, 0.00776f, 0.00798f, 0.00820f,
				0.00842f, 0.00866f, 0.00889f, 0.00914f, 0.00939f, 0.00964f, 0.00990f, 0.01017f, 0.01044f, 0.01072f,
				0.01101f, 0.01130f, 0.01160f, 0.01191f, 0.01222f, 0.01255f, 0.01287f, 0.01321f, 0.01355f, 0.01390f,
				0.01426f, 0.01463f, 0.01500f, 0.01539f, 0.01578f, 0.01618f, 0.01659f, 0.01700f, 0.01743f, 0.01786f,
				0.01831f, 0.01876f, 0.01923f, 0.01970f, 0.02018f, 0.02068f, 0.02118f, 0.02169f, 0.02222f, 0.02275f,
				0.02330f, 0.02385f, 0.02442f, 0.02500f, 0.02559f, 0.02619f, 0.02680f, 0.02743f, 0.02807f, 0.02872f,
				0.02938f, 0.03005f, 0.03074f, 0.03144f, 0.03216f, 0.03288f, 0.03362f, 0.03438f, 0.03515f, 0.03593f,
				0.03673f, 0.03754f, 0.03836f, 0.03920f, 0.04006f, 0.04093f, 0.04182f, 0.04272f, 0.04363f, 0.04457f,
				0.04551f, 0.04648f, 0.04746f, 0.04846f, 0.04947f, 0.05050f, 0.05155f, 0.05262f, 0.05370f, 0.05480f,
				0.05592f, 0.05705f, 0.05821f, 0.05938f, 0.06057f, 0.06178f, 0.06301f, 0.06426f, 0.06552f, 0.06681f,
				0.06811f, 0.06944f, 0.07078f, 0.07215f, 0.07353f, 0.07493f, 0.07636f, 0.07780f, 0.07927f, 0.08076f,
				0.08226f, 0.08379f, 0.08534f, 0.08692f, 0.08851f, 0.09012f, 0.09176f, 0.09342f, 0.09510f, 0.09680f,
				0.09853f, 0.10027f, 0.10204f, 0.10383f, 0.10565f, 0.10749f, 0.10935f, 0.11123f, 0.11314f, 0.11507f,
				0.11702f, 0.11900f, 0.12100f, 0.12302f, 0.12507f, 0.12714f, 0.12924f, 0.13136f, 0.13350f, 0.13567f,
				0.13786f, 0.14007f, 0.14231f, 0.14457f, 0.14686f, 0.14917f, 0.15151f, 0.15386f, 0.15625f, 0.15866f,
				0.16109f, 0.16354f, 0.16602f, 0.16853f, 0.17106f, 0.17361f, 0.17619f, 0.17879f, 0.18141f, 0.18406f,
				0.18673f, 0.18943f, 0.19215f, 0.19489f, 0.19766f, 0.20045f, 0.20327f, 0.20611f, 0.20897f, 0.21186f,
				0.21476f, 0.21770f, 0.22065f, 0.22363f, 0.22663f, 0.22965f, 0.23270f, 0.23576f, 0.23885f, 0.24196f,
				0.24510f, 0.24825f, 0.25143f, 0.25463f, 0.25785f, 0.26109f, 0.26435f, 0.26763f, 0.27093f, 0.27425f,
				0.27760f, 0.28096f, 0.28434f, 0.28774f, 0.29116f, 0.29460f, 0.29806f, 0.30153f, 0.30503f, 0.30854f,
				0.31207f, 0.31561f, 0.31918f, 0.32276f, 0.32636f, 0.32997f, 0.33360f, 0.33724f, 0.34090f, 0.34458f,
				0.34827f, 0.35197f, 0.35569f, 0.35942f, 0.36317f, 0.36693f, 0.37070f, 0.37448f, 0.37828f, 0.38209f,
				0.38591f, 0.38974f, 0.39358f, 0.39743f, 0.40129f, 0.40517f, 0.40905f, 0.41294f, 0.41683f, 0.42074f,
				0.42465f, 0.42858f, 0.43251f, 0.43644f, 0.44038f, 0.44433f, 0.44828f, 0.45224f, 0.45620f, 0.46017f,
				0.46414f, 0.46812f, 0.47210f, 0.47608f, 0.48006f, 0.48405f, 0.48803f, 0.49202f, 0.49601f, 0.50000f,
			};

			const float min_standard_normal = (::logf(static_cast<float>(min)) - log_median) / log_standard_deviation;
			const int index_of_min = static_cast<int>(min_standard_normal * 100.f + .5f) + 410;
			const float certainty_of_min = 1.f - (index_of_min < 0 ? 0.00000f : table[index_of_min]);

			float best_estimate = ::expf(-1.666666f * log_standard_deviation + log_median);

			char buffer[] =
				" min: zxxx.yy?s  acc: zxxx.yy?%  tsc: zxxx.yy?c  ???: zxxx.yy?c  ???: zxxx.yy?c\n"
				" exp: zxxx.yy?s  mdn: zxxx.yy?s  mod: zxxx.yy?s  var: zxxx.yy?s  95%: zxxx.yy?s\n"
				" ins: ????????n cache:????????n cache:????????m  jmp: ????????n  jmp: ????????m\n";
			write_time_and_unit(info, buffer + 6, min);
			write_timeless_number(buffer + 6 + 16, static_cast<utime>(certainty_of_min * 1000000.f + .5f), 10000);
			write_timeless_number(buffer + 6 + 32, min);

			write_time_and_unit(info, buffer + 86 + 16, static_cast<utime>(::expf(log_median) + .5f));
			write_time_and_unit(info, buffer + 86 + 48, static_cast<utime>((::expf(log_standard_deviation * log_standard_deviation) - 1.f) * ::expf(2.f * log_median + log_standard_deviation * log_standard_deviation) + .5f));
			write_time_and_unit(info, buffer + 86 + 64, static_cast<utime>(best_estimate + .5f));

			write_timeless_number(buffer + 166, (utime)fd0[0]);
			write_timeless_number(buffer + 166 + 16, (utime)fd1[1]);
			write_timeless_number(buffer + 166 + 32, (utime)fd2[2]);
			write_timeless_number(buffer + 166 + 48, (utime)fd3[3]);
			write_timeless_number(buffer + 166 + 64, (utime)fd4[4]);
			::write(STDOUT_FILENO, buffer, sizeof buffer - 1);

			usize histogram[78] = {};
			const float log_min = static_cast<float>(min);
			const float log_max = static_cast<float>(max); // todo next float, should be exclusive
			usize bin_count = (max - min) + 1;
			if (bin_count > 78) bin_count = 78;
			const float bin_size = (log_max - log_min) / static_cast<float>(bin_count);

			usize maxcount = 0;
			for (usize i = 0; i < nsamples; i++)
			{
				int bin = int((static_cast<float>(res[i]) - log_min) / bin_size);
				if (bin >= 78) bin = 77;

				histogram[bin]++;
				if (histogram[bin] > maxcount)
				{
					maxcount = histogram[bin];
				}
			}

			for (usize bin = 0; bin < bin_count; bin++)
			{
				histogram[bin] = (histogram[bin] * 6 + maxcount - 1) / maxcount;
			}

			char histogram_line[1 + 78 + 1];
			histogram_line[0] = ' ';
			histogram_line[1 + bin_count] = '\n';
			for (usize line = 0; line < 7; line++)
			{
				for (usize bin = 0; bin < bin_count; bin++)
				{
					histogram_line[bin + 1] = histogram[bin] > line ? '#' : ' ';
				}
				::write(STDOUT_FILENO, histogram_line, 1 + bin_count + 1);
			}
		}
	};

	struct impl
	{
		cpu_info info;

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

			// init info
			{
				unsigned int model;
				unsigned int family;
				unsigned int model_ext;
				unsigned int family_ext;

				{
					static const char buffer[] = "cpu info\n";
					::write(STDOUT_FILENO, buffer, sizeof buffer - 1);
				}

				{
					unsigned int n = 0x01;
					unsigned int regs[4];
					__get_cpuid(n, regs + 0, regs + 1, regs + 2, regs + 3);

					model = (regs[0] >> 4) & 0xf;
					family = (regs[0] >> 8) & 0xf;
					model_ext = (regs[0] >> 16) & 0xf;
					family_ext = (regs[0] >> 20) & 0xff;

					::printf(" model: 0x%x, ext: 0x%x\n", model, model_ext);
					::printf(" family: 0x%x, ext: 0x%x\n", family, family_ext);
				}

				{
					unsigned int n = 0x15;
					unsigned int regs[4];
					__get_cpuid(n, regs + 0, regs + 1, regs + 2, regs + 3);

					if (regs[1] != 0)
					{
						::printf(" TSC / \"core crystal clock\" ratio: %u / %u\n", regs[1], regs[0]);
					}
					else
					{
						// todo error
					}

					unsigned int core_crystal_clock_frequency;
					if (regs[2] != 0)
					{
						core_crystal_clock_frequency = regs[2];
					}
					else
					{
						if (family == 0x6 && family_ext == 0x55)
						{
							core_crystal_clock_frequency = 25000000u;
						}
						else if (family == 0x6 && family_ext == 0x5c)
						{
							core_crystal_clock_frequency = 19200000u;
						}
						else
						{
							core_crystal_clock_frequency = 24000000u;
						}
					}
					::printf(" nominal frequency of the core crystal clock: %uHz\n", core_crystal_clock_frequency);

					info.tsc_frequency = (utime)core_crystal_clock_frequency * regs[1] / regs[0];
					::printf(" TSC frequency: %luHz\n", info.tsc_frequency);
				}

				const char newline = '\n';
				::write(STDOUT_FILENO, &newline, sizeof newline);
			}
		}

		static impl & get()
		{
			static impl x;
			return x;
		}

		exec unit(cstr unit)
		{
			return exec(info, unit, nsamples);
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

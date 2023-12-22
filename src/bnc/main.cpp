#include <dmb/benchmark.hpp>

#include <string>

#if defined(_WIN32)
# include <Windows.h>
#endif

#if !defined(_WIN32)
// note we disable clock accuracy tests on Windows due to its many
// problems with accurate timing and sleeping
//
// the attached link is only one of many sources that try to
// understand and educate about the situation
//
// https://randomascii.wordpress.com/2020/10/04/windows-timer-resolution-the-great-rule-change/
# include <thread>

DMB_SAMPLE("clock accuracy") // todo on Windows, hide by default, never disable
{

	DMB_CASE("100 micro")()
	{
		std::this_thread::sleep_for(std::chrono::microseconds(100));
		return 0; // todo remove
	};

	DMB_CASE("10 micro")()
	{
		std::this_thread::sleep_for(std::chrono::microseconds(10));
		return 0; // todo remove
	};

	DMB_CASE("1 micro")()
	{
		std::this_thread::sleep_for(std::chrono::microseconds(1));
		return 0; // todo remove
	};

}
#endif

DMB_SAMPLE("clock overhead")
{

	DMB_CASE("RDTSC overhead")()
	{
		return __rdtsc();
	};

#if defined(_WIN32)
	DMB_CASE("QueryPerformanceCounter overhead")()
	{
		LARGE_INTEGER ticks;
		::QueryPerformanceCounter(&ticks);
		return ticks.QuadPart;
	};
#endif

}

DMB_SAMPLE("")
{

	DMB_CASE("default construction time of std::string")()
	{
		return std::string();
	};

	DMB_CASE("default lifetime of std::string")()
	{
		return dmb::nop(std::string());
	};

	static const char long_string[] = "<><> abcdefghijklmnopqrstuvwxyz <><>";

	DMB_CASE("long construction time of std::string")()
	{
		return std::string(long_string);
	};

	DMB_CASE("default lifetime of std::string")()
	{
		return dmb::nop(std::string(long_string));
	};

}

DMB_SAMPLE("arithmetic")
{

	DMB_CASE("baseline")(dmb::idx i)
	{
		return i;
	};

	DMB_CASE("addition")(dmb::u64 x, dmb::u64 y)
	{
		return x + y;
	};

	DMB_CASE("multiplication")(dmb::u64 x, dmb::u64 y)
	{
		return x * y;
	};

	DMB_CASE("division")(dmb::u64 x, dmb::pos<dmb::u64> y)
	{
		return x / y;
	};

	DMB_CASE("modulo")(dmb::u64 x, dmb::pos<dmb::u64> y)
	{
		return x % y;
	};

}

int main(int argc, const char * argv[])
{
	dmb::impl & x = dmb::impl::get();

	for (int argi = 1; argi < argc;)
	{
		if (argv[argi][0] == '-')
		{
			if (argv[argi][1] == '-')
			{
				if (argv[argi][2] == 'i' && argv[argi][3] == 'n' && argv[argi][4] == 'f' && argv[argi][5] == 'o' && argv[argi][6] == '\0')
				{
					x.flags |= 1;
					argi++;
				}
				else
				{
					::fprintf(stderr, "error: the argument \"%s\" is unknown\n", argv[argi]);
					return -1;
				}
			}
			else if (argv[argi][1] == 'i' && argv[argi][2] == '\0')
			{
				x.flags |= 1;
				argi++;
			}
			else
			{
				::fprintf(stderr, "error: the argument \"%s\" is unknown\n", argv[argi]);
				return -1;
			}
		}
		else
		{
			::fprintf(stderr, "error: the argument \"%s\" is unknown\n", argv[argi]);
			return -1;
		}
	}

	x.run();

	x.nsamples = argc < 1000 ? 1000 : static_cast<dmb::usize>(argc);

	dmb::usize n = x.nfuncs;
	if (n > 0)
	{
		do
		{
			n--;

			x.funcs[n](x);
		}
		while (n > 0);
	}

	return 0;
}

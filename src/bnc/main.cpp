#include <dmb/benchmark.hpp>

#include <string>
#include <thread>

int c;

DMB_SAMPLE("clock test")
{

	DMB_CASE("1 milli")()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return 0;
	};

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

#include <cpuid.h>

int main(int argc, const char * argv[])
{
	dmb_unused(argv);

	using reg_type = unsigned int;
	reg_type a[4];
	{
		unsigned int n = 0x15;
		__get_cpuid(n, a + 0, a + 1, a + 2, a + 3);
		if (a[1] != 0)
		{
			::printf("TSC/\"core crystal clock\" ratio: %d/%d\n", a[1], a[0]);
		}
		if (a[2] != 0)
		{
			::printf("nominal frequency of the core crystal clock: %dHz\n", a[2]);
		}
		// "TSC frequency" = "core crystal clock frequency" * a[1]/a[0]
	}
	{
		unsigned int n = 0x16;
		__get_cpuid(n, a + 0, a + 1, a + 2, a + 3);
		if ((unsigned short)a[0] != 0)
		{
			::printf("processor base frequency: %dMhz\n", (unsigned short)a[0]);
		}
		if ((unsigned short)a[1] != 0)
		{
			::printf("maximum frequency: %dMhz\n", (unsigned short)a[1]);
		}
		if ((unsigned short)a[2] != 0)
		{
			::printf("bus reference frequency: %dMhz\n", (unsigned short)a[2]);
		}
	}

	c = argc;

	dmb::impl & x = dmb::impl::get();
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

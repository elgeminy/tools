
#include "pch.h"
#include "direnum.h"
#include "comparer.h"

#define ARG_PREFIX		L'-'
#define ARG_USAGE		L"-?"
#define ARG_HASH		L"-hash"
#define ARG_VERBOSE		L"-v"

#define ARG_INC_FILES	L"-if"
#define ARG_INC_DIRS	L"-id"
#define ARG_EXC_FILES	L"-ef"
#define ARG_EXC_DIRS	L"-ed"

#define ARG_MIN_SIZE	L"-min"
#define ARG_MAX_SIZE	L"-max"


bool Usage (std::wstring error, bool sys_fail = false)
{
	std::vector <wchar_t> path (MAX_32PATH);
	const wchar_t * pfile = nullptr;
	if (::GetModuleFileName (nullptr, path.data (), MAX_32PATH) > 0)
		pfile = ::PathFindFileName (path.data ());
	if (nullptr == pfile)
		pfile = L"FileComparer.exe";

	if (!error.empty ())
	{
		if (sys_fail)
			std::wcout << L"System failure in func " << error << L", error " << ::GetLastError () << std::endl;
		else
			std::wcout << L"Invalid command: " << error << std::endl;
	}
	std::wcout 
		<< "Usage:\n\n"
		<< pfile <<
			L"  [<file_or_dir_1> ... <file_or_dir_N>] [" ARG_VERBOSE L"] [" ARG_HASH L"] [" ARG_INC_FILES L"] [" ARG_INC_DIRS L"] [" ARG_EXC_FILES L"] [" ARG_EXC_DIRS L"] [" ARG_MIN_SIZE L"] [" ARG_MAX_SIZE L"]\n\n"
			L"  <file_or_dir_1...N>\t\t\t- list of files and/or directories to scan for comparing files. Omit to scan current directory\n"
			L"  " ARG_VERBOSE L"\t\t\t\t- verbose output\n"
			L"  " ARG_HASH L"\t\t\t\t- print equal files hash\n"
			L"  " ARG_EXC_FILES L" <mask_1> ... <mask_N>\t- list of files' masks to skip from comparing (symbols (*) and (?) are allowed)\n"
			L"  " ARG_EXC_DIRS L" <mask_1> ... <mask_N>\t- list of dirs' masks to skip from comparing (symbols (*) and (?) are allowed)\n"
			L"  " ARG_INC_FILES L" <mask_1> ... <mask_N>\t- list of files' masks to include in comparing (symbols (*) and (?) are allowed)\n"
			L"  " ARG_INC_DIRS L" <mask_1> ... <mask_N>\t- list of dirs' masks to include in comparing (symbols (*) and (?) are allowed)\n"
			L"  \tmasks will not be applied to directly added files\n"
			L"  " ARG_MIN_SIZE L" <file_size>\t\t- min file size to include in comparing\n"
			L"  " ARG_MAX_SIZE L" <file_size>\t\t- max file size to include in comparing\n"
		L"\n"
	;

	return false;
}

bool ReadArg (DirEnumerator & de, bool & hash, bool & verbose)
{
	int num = 0;
	wchar_t ** ppcmd = CommandLineToArgvW (::GetCommandLine (), &num);
	if (nullptr == ppcmd)
		return Usage (L"CommandLineToArgvW", true);
	std::unique_ptr <wchar_t *, decltype(&LocalFree)> pp (ppcmd, LocalFree);

	auto ReadList = [&ppcmd, &num](int & n, ListOfStrings & list)
	{
		list.clear ();
		for (; n < num; n++)
		{
			const wchar_t * ptr = ppcmd[n];
			if (ARG_PREFIX == *ptr)
			{
				return !list.empty ();
			}

			list.emplace_back (ptr);
		}

		return true;
	};

	auto ReadSize = [&ppcmd](int & n, uintmax_t & size)
	{
		const wchar_t * ptr = ppcmd[n++];
		if (ARG_PREFIX == *ptr)
			return false;

		try
		{
			size = std::stoull (ptr);
		}
		catch (...)
		{
			return false;
		}

		return true;
	};

	ListOfStrings list;
	uintmax_t min_size = 0, max_size = (uintmax_t)-1;

	enum Type
	{ 
		type_list, type_bool, type_size 
	};
	struct
	{
		Type type;
		const wchar_t * arg = nullptr;
		void * data = nullptr;
		std::function <void (ListOfStrings &)> callback;
	} 
	args [] = 
	{
		{ type_bool, ARG_HASH, &hash, nullptr },
		{ type_bool, ARG_VERBOSE, &verbose, nullptr },
		{ type_list, ARG_EXC_FILES, nullptr, std::bind (&DirEnumerator::AddExcludeFiles, &de, std::placeholders::_1)  },
		{ type_list, ARG_EXC_DIRS, nullptr, std::bind (&DirEnumerator::AddExcludeDirectories, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_FILES, nullptr, std::bind (&DirEnumerator::AddIncludeFiles, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_DIRS, nullptr, std::bind (&DirEnumerator::AddIncludeDirectories, &de, std::placeholders::_1) },
		{ type_size, ARG_MIN_SIZE, &min_size, nullptr },
		{ type_size, ARG_MAX_SIZE, &max_size, nullptr },
	};

	bool scan_dir_found = false;

	for (int n = 1; n < num; )
	{
		const wchar_t * ptr = ppcmd[n];

		if (_wcsicmp (ptr, ARG_USAGE) == 0)
		{
			return Usage (L"");
		}

		bool arg_found = false;
		bool arg_valid = true;

		for (auto & arg : args)
		{
			if (_wcsicmp (ptr, arg.arg) == 0)
			{
				arg_found = true;
				switch (arg.type)
				{
				case type_list:
					if (arg_valid = ReadList (++n, list) && arg.callback != nullptr)
						arg.callback (list);
					break;
				case type_size:
					arg_valid = ReadSize (++n, *((uintmax_t *)arg.data));
					break;
				case type_bool:
					*((bool *)arg.data) = true;
					n++;
					break;
				default:
					return Usage (L"invalid logic");
				}
			}
			if (arg_found)
				break;
		}

		if (!arg_found)
		{
			if (arg_valid = ReadList (n, list))
			{
				scan_dir_found = true;
				if (!de.SetScanDirectories (list))
					return false;
			}
		}

		if (!arg_valid)
			return Usage (L"invalid argument");
	}

	if (!scan_dir_found)
	{
		if (!de.SetScanDirectories ({ std::filesystem::current_path () }))
			return false;
	}

	de.SetFileLimit (min_size, max_size);

	return true;
}

void PrintEqualGroup (const ListOfFiles & group)
{
	static size_t igroup = 0;
	if (group.empty ())
	{
		if (0 == igroup)
			std::wcout << L"No equal files found\n";
		return;
	}

	std::wcout << L"Group of equal files #" << ++igroup << L", file size " << group.begin ()->SizeFormatted ();
	if (!group.begin ()->Hash ().empty ())
		std::cout << ". Files hash: " << group.begin ()->Hash ();
	std::wcout << std::endl;
	for (const auto & file : group)
	{
		wprintf (L"%s\n", file.Path ());
	}
	std::wcout << std::endl;
}

void PrintFailedFiles (const ListOfFiles & failed)
{
	std::wcout << std::endl;

	auto FormatError = [](DWORD error) -> std::wstring
	{
		void * buff = nullptr;
		FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 
			MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buff, 0, nullptr);
		std::unique_ptr <void, std::function <void (void *)>> pp (buff, [](void * buff) { LocalFree (buff); });
		return (wchar_t *)buff;
	};

	if (!failed.empty ())
	{
		std::wcout << L"There are number of files are failed to open:\n";
		for (auto & file : failed)
		{
			wprintf (L"%s, error %u: %s", file.Path (), file.Error (), FormatError (file.Error ()).c_str ());
		}
	}
}

struct DirEnumHandler : public IDirEnumHandler
{
	void OnGivenPathFail (const std::wstring & file, std::wstring error)
	{
		Usage (file + L": " + error);
	}
	void OnFileFound (std::filesystem::path && file, uintmax_t size)
	{
		found[size].push_back (std::move (file));
	}
	void OnFileFound (const std::filesystem::path & file, uintmax_t size)
	{
		found[size].push_back (std::filesystem::path (file));
	}
	void OnFileIgnored (std::filesystem::path && file)
	{
		ignored.push_back (std::move (file));
	}
	void OnFileIgnored (const std::filesystem::path & file)
	{
		ignored.push_back (file);
	}

	void OnScanError (const std::string & error)
	{
		printf ("%s\n", error.c_str ());
	}

	ListOfStrings ignored;
	std::map <uintmax_t, ListOfFiles> found;
};

int main ()
{
	::_wsetlocale (LC_CTYPE, L"");

	try
	{
		DirEnumHandler handler;
		DirEnumerator de (&handler);
		bool hash = false, verbose = false;
		if (!ReadArg (de, hash, verbose))
			return 0;

		std::wcout << L"Start scanning directories...\n";

		de.EnumerateDirectory ();

		if (verbose)
		{
			for (auto & file : handler.ignored)
			{
				wprintf (L"Object '%s' ignored\n", file.c_str ());
			}
		}

		size_t found_count = 0, ready_count = 0;
		for (auto & f : handler.found)
		{
			found_count += f.second.size ();
			if (f.second.size () > 1)
				ready_count += f.second.size ();
		}
		std::wcout 
			<< found_count << L" files found. " 
			<< ready_count << L" of them ready to compare. "
			<< handler.ignored.size () << " objects ignored.\n"
			<< L"Start comparing...\n\n";

		std::list <ListOfFiles> equal;
		ListOfFiles failed;

		Comparer cmp (handler.found, hash);
		cmp.FindEqualFiles (equal, failed, PrintEqualGroup);

		PrintEqualGroup ({});
		PrintFailedFiles (failed);
	}
	catch (EnumException & ex)
	{
		std::cout << "Unexpected error occuped: " << ex.error << std::endl;
	}
	catch (...)
	{
		std::wcout << L"Unexpected error occuped" << std::endl;
	}

	return 0;
}

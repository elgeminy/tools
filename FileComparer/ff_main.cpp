
#include "pch.h"
#include "direnum.h"
#include "file.h"

#define ARG_PREFIX		L'-'
#define ARG_USAGE		L"-?"
#define ARG_HASH		L"-hash"
#define ARG_SIZE		L"-size"

#define ARG_SCAN_PATH	L"-p"

#define ARG_INC_DIRS	L"-id"
#define ARG_EXC_DIRS	L"-ed"
#define ARG_EXC_FILES	L"-ef"
#define ARG_INC_PATHS	L"-ip"
#define ARG_EXC_PATHS	L"-ep"

#define ARG_MIN_SIZE	L"-min"
#define ARG_MAX_SIZE	L"-max"

#define ARG_GROUP_EXT	L"-ge"
#define ARG_GROUP_SIZE	L"-gs"
#define ARG_GROUP_HASH	L"-gh"


bool Usage (std::wstring error, bool sys_fail = false)
{
	std::vector <wchar_t> path (MAX_32PATH);
	const wchar_t * pfile = nullptr;
	if (::GetModuleFileName (nullptr, path.data (), MAX_32PATH) > 0)
		pfile = ::PathFindFileName (path.data ());
	if (nullptr == pfile)
		pfile = L"FileFinder.exe";

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
			L"  [<file_1> ... <file_N>] [" ARG_SCAN_PATH L"] [" ARG_HASH L"] [" ARG_SIZE L"] [" ARG_INC_DIRS L"] [" ARG_EXC_DIRS L"] [" ARG_EXC_FILES L"] [" ARG_INC_PATHS L"] [" ARG_EXC_PATHS L"] [" ARG_MIN_SIZE L"] [" ARG_MAX_SIZE L"] [" ARG_GROUP_EXT L"] [" ARG_GROUP_HASH L"] [" ARG_GROUP_SIZE L"]\n\n"
			L"  <file_1...N>\t\t\t- list of files to search (symbols (*) and (?) are allowed). Omit to find all files (e.g. '*')\n"
			L"  " ARG_HASH L"\t\t\t\t- print found files hash\n"
			L"  " ARG_SCAN_PATH L" <dir_path>\t\t\t- path to a directory to search in. Currect dir used by default\n"
			L"  " ARG_EXC_FILES L" <mask_1> ... <mask_N>\t- list of files' masks to skip from searching (symbols (*) and (?) are allowed)\n"
			L"  " ARG_EXC_DIRS L" <mask_1> ... <mask_N>\t- list of dirs' masks to skip from searching (symbols (*) and (?) are allowed)\n"
			L"  " ARG_EXC_PATHS L" <mask_1> ... <mask_N>\t- list of paths' masks to skip from searching (symbols (*) and (?) are allowed)\n"
			L"  " ARG_INC_DIRS L" <mask_1> ... <mask_N>\t- list of dirs' masks to search in (symbols (*) and (?) are allowed)\n"
			L"  " ARG_INC_PATHS L" <mask_1> ... <mask_N>\t- list of path' masks to search in (symbols (*) and (?) are allowed)\n"
			L"  " ARG_MIN_SIZE L" <file_size>\t\t- min file size to search\n"
			L"  " ARG_MAX_SIZE L" <file_size>\t\t- max file size to search\n"
			L"  " ARG_GROUP_EXT L"\t\t\t\t- print found files grouping by extension\n"
			L"  " ARG_GROUP_SIZE L"\t\t\t\t- print found files grouping by size\n"
			L"  " ARG_GROUP_HASH L"\t\t\t\t- print found files grouping by hash\n"
		L"\n"
	;

	return false;
}

struct Opt
{
	bool find_dirs = false;
	bool verbose = false;
	bool print_hash = false;
	bool print_size = false;
	bool group_size = false;
	bool group_ext = false;
	bool group_hash = false;
};

bool ReadArg (DirEnumerator & de, Opt & opts)
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
		{ type_bool, ARG_HASH, &opts.print_hash, nullptr },
		{ type_bool, ARG_SIZE, &opts.print_size, nullptr },
		{ type_bool, ARG_GROUP_SIZE, &opts.group_size, nullptr },
		{ type_bool, ARG_GROUP_EXT, &opts.group_ext, nullptr },
		{ type_bool, ARG_GROUP_HASH, &opts.group_hash, nullptr },
		{ type_list, ARG_EXC_FILES, nullptr, std::bind (&DirEnumerator::AddExcludeFiles, &de, std::placeholders::_1) },
		{ type_list, ARG_EXC_DIRS, nullptr, std::bind (&DirEnumerator::AddExcludeDirectories, &de, std::placeholders::_1) },
		{ type_list, ARG_EXC_PATHS, nullptr, std::bind (&DirEnumerator::AddExcludePaths, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_DIRS, nullptr, std::bind (&DirEnumerator::AddIncludeDirectories, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_PATHS, nullptr, std::bind (&DirEnumerator::AddIncludePaths, &de, std::placeholders::_1) },
		{ type_list, ARG_SCAN_PATH, nullptr, std::bind (&DirEnumerator::SetScanDirectories, &de, std::placeholders::_1) },
		{ type_size, ARG_MIN_SIZE, &min_size, nullptr },
		{ type_size, ARG_MAX_SIZE, &max_size, nullptr },
	};

	bool search_file_found = false, scan_dir_found = false;

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

					if (_wcsicmp (ptr, ARG_SCAN_PATH) == 0)
						scan_dir_found = true;
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
				search_file_found = true;
				de.AddIncludeFiles (list);
			}
		}

		if (!arg_valid)
			return Usage (L"invalid argument");
	}

	if (!scan_dir_found)
	{
		de.SetScanDirectories ({ std::filesystem::current_path () });
	}
	if (!search_file_found)
	{
		ListOfStrings s { L"*" };
		de.AddIncludeFiles (s);
	}

	de.SetFileLimit (min_size, max_size);

	if (((size_t)opts.group_ext + (size_t)opts.group_hash + (size_t)opts.group_size) > 1)
		return Usage (L"Pls use only one group parameter");

	return true;
}

void PrintFailedFile (const File & failed)
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

	wprintf (L"Failed: '%s', error %u: %s", failed.Path (), failed.Error (), FormatError (failed.Error ()).c_str ());
}

struct DirEnumHandler : public IDirEnumHandler
{
	DirEnumHandler (std::function <void (File *)> fc) :
		file_callback (fc)
	{}

	void OnGivenPathFail (const std::wstring & file, std::wstring error)
	{
		Usage (file + L": " + error);
	}

	void OnFileFound (std::filesystem::path && file, uintmax_t size)
	{
		found.emplace_back (std::move (file), size);
		file_callback (&(found.back ()));
	}

	void OnFileFound (const std::filesystem::path & file, uintmax_t size)
	{
		found.emplace_back (std::filesystem::path (file), size);
		file_callback (&(found.back ()));
	}

	void OnFileIgnored (std::filesystem::path &&) { }
	void OnFileIgnored (const std::filesystem::path &) { }

	void OnScanError (const std::string & error)
	{
		printf ("%s\n", error.c_str ());
	}

	std::function <void (File *)> file_callback;
	ListOfFiles found;
};


void CalcHashAndPrint (File * file, bool print_size)
{
	static std::mutex gm;

	auto Hash = [print_size](File * file)
	{
		file->CalcHash ();

		std::lock_guard <std::mutex> lk (gm);

		if (file->Failed ())
			PrintFailedFile (*file);
		else if (print_size)
			wprintf (L"[%S] [%s]\n%s\n", file->Hash ().c_str (), file->SizeFormatted ().c_str (), file->Path ());
		else
			wprintf (L"[%S]\n%s\n", file->Hash ().c_str (), file->Path ());

	};

	std::async (Hash, file);
}

void CalcHashes (ListOfFiles & files)
{
	auto Hashing = [](File * file, std::promise <File *> && p)
	{
		file->CalcHash ();
		p.set_value (file);
	};

	std::list <std::future <File *>> futures;
	for (auto & file : files)
	{
		std::promise <File *> p;
		futures.emplace_back (p.get_future ());
		std::async (Hashing, &file, std::move (p));
	}

	for (auto & fut : futures)
	{
		fut.wait ();
	}
}

void PrintGrouped (const Opt & opts, ListOfFiles & files)
{
	if (opts.group_size)
	{
		if (opts.print_hash)
			CalcHashes (files);

		std::map <uintmax_t, std::list <File *>> group;
		for (auto & file : files)
		{
			group[file.Size ()].push_back (&file);
		}

		for (auto & g : group)
		{
			for (auto file : g.second)
			{
				if (opts.print_hash)
					wprintf (L"[%S]\n%s [%s]\n", file->Hash ().c_str (), file->Path (), file->SizeFormatted ().c_str ());
				else
					wprintf (L"%s [%s]\n", file->Path (), file->SizeFormatted ().c_str ());
			}
		}
	}
	else if (opts.group_ext)
	{
		if (opts.print_hash)
			CalcHashes (files);

		auto MakeLower = [](std::wstring & buff)
		{
			std::transform (buff.begin (), buff.end (), buff.begin (),
				[](wchar_t c)
				{
					return std::towlower (c);
				}
			);
		};

		std::map <std::wstring, std::list <File *>> group;
		for (auto & file : files)
		{
			auto ext = file.Ext ();
			MakeLower (ext);
			group[ext].push_back (&file);
		}

		for (auto & g : group)
		{
			for (auto file : g.second)
			{
				if (opts.print_hash)
					wprintf (L"[%S]\n", file->Hash ().c_str ());
				wprintf (L"%s", file->Path ());

				if (opts.print_size)
					wprintf (L" [%s]", file->SizeFormatted ().c_str ());
				wprintf (L"\n");
			}
		}
	}
	else if (opts.group_hash)
	{
		CalcHashes (files);

		std::map <std::string, std::list <File *>> group;
		for (auto & file : files)
		{
			auto ext = file.Ext ();
			group[file.Hash ()].push_back (&file);
		}

		for (auto & g : group)
		{
			wprintf (L"[%S]\n", g.first.c_str ());
			for (auto file : g.second)
			{
				if (opts.print_size)
					wprintf (L"%s [%s]\n", file->Path (), file->SizeFormatted ().c_str ());
				else
					wprintf (L"%s\n", file->Path ());
			}
		}
	}
}

int main ()
{
	::_wsetlocale (LC_CTYPE, L"");

	try
	{
		Opt opts;
		auto PrintFile = [&opts, hash_func = CalcHashAndPrint](File * file)
		{
			if (opts.group_ext || opts.group_hash || opts.group_size)
				return;

			if (!opts.print_hash)
			{
				if (opts.print_size)
					wprintf (L"%s [%s]\n", file->Path (), file->SizeFormatted ().c_str ());
				else
					wprintf (L"%s\n", file->Path ());

				return;
			}

			hash_func (file, opts.print_size);
		};

		DirEnumHandler handler (PrintFile);
		DirEnumerator de (&handler);
		bool hash = false, verbose = false;
		if (!ReadArg (de, opts))
			return 0;

		std::wcout << L"Start scanning directories...\n";
		de.EnumerateDirectory ();

		if (opts.group_ext || opts.group_hash || opts.group_size)
			PrintGrouped (opts, handler.found);


		std::wcout << L"\n" << handler.found.size () << L" files found\n";
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

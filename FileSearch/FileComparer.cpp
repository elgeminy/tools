
#include "pch.h"
#include "filecomparer.h"

bool ReadArg (DirEnumerator & de, bool find_all_hashes, UsageFunc usage_foo)
{
	int num = 0;
	wchar_t ** ppcmd = CommandLineToArgvW (::GetCommandLine (), &num);
	if (nullptr == ppcmd)
		return usage_foo (L"CommandLineToArgvW", true);
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
		{ type_list, ARG_EXC_FILES, nullptr, std::bind (&DirEnumerator::AddExcludeFiles, &de, std::placeholders::_1) },
		{ type_list, ARG_EXC_DIRS, nullptr, std::bind (&DirEnumerator::AddExcludeDirectories, &de, std::placeholders::_1) },
		{ type_list, ARG_EXC_PATHS, nullptr, std::bind (&DirEnumerator::AddExcludePaths, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_FILES, nullptr, std::bind (&DirEnumerator::AddIncludeFiles, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_DIRS, nullptr, std::bind (&DirEnumerator::AddIncludeDirectories, &de, std::placeholders::_1) },
		{ type_list, ARG_INC_PATHS, nullptr, std::bind (&DirEnumerator::AddIncludePaths, &de, std::placeholders::_1) },
		{ type_size, ARG_MIN_SIZE, &min_size, nullptr },
		{ type_size, ARG_MAX_SIZE, &max_size, nullptr },
		{ type_bool, ARG_PRINT_HASH, &find_all_hashes, nullptr }
	};

	bool scan_dir_found = false;

	for (int n = 1; n < num; )
	{
		const wchar_t * ptr = ppcmd[n];

		if (_wcsicmp (ptr, ARG_USAGE) == 0)
		{
			return usage_foo (L"", false);
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
					return usage_foo (L"invalid logic", false);
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
			return usage_foo (L"invalid argument", false);
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
		std::wcout << L". Files hash: " << group.begin ()->Hash ();
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

void DirEnumHandler::OnGivenPathFail (const std::wstring & file, std::wstring error)
{
	_usage (file + L": " + error, false);
}

void DirEnumHandler::OnFileFound (std::filesystem::path && file, uintmax_t size)
{
	_files [size].emplace_back (std::move (file), size);
}

void DirEnumHandler::OnFileFound (const std::filesystem::path & file, uintmax_t size)
{
	_files [size].emplace_back (std::filesystem::path (file), size);
}

void DirEnumHandler::OnScanError (const std::string & error)
{
	printf ("%s\n", error.c_str ());
}
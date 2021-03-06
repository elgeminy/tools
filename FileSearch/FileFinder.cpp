
#include "pch.h"
#include "filefinder.h"

bool ReadArg (DirEnumerator & de, Opt & opts, UsageFunc usage_foo)
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

	auto ReadSize = [&ppcmd, &num](int & n, uintmax_t & size)
	{
		if (n >= num)
			return false;

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

	auto ReadString = [&ppcmd, &num](int & n, std::wstring & str)
	{
		if (n >= num)
			return false;

		const wchar_t * ptr = ppcmd[n++];
		if (ARG_PREFIX == *ptr)
			return false;

		str = ptr;
		return true;
	};

	ListOfStrings list;
	uintmax_t min_size = 0, max_size = (uintmax_t)-1;
	std::wstring content;

	enum Type
	{ 
		type_list, type_bool, type_size, type_string 
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
		{ type_bool, ARG_PRINT_HASH, &opts.print_hash, nullptr },
		{ type_bool, ARG_PRINT_SIZE, &opts.print_size, nullptr },
		{ type_bool, ARG_GROUP_SIZE, &opts.group_size, nullptr },
		{ type_bool, ARG_GROUP_EXT, &opts.group_ext, nullptr },
		{ type_bool, ARG_GROUP_HASH, &opts.group_hash, nullptr },
		{ type_list, ARG_HASH, &opts.hashes, nullptr },
		{ type_string, ARG_CONTENT, &content, nullptr },
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
					arg_valid = ReadList (++n, list);

					if (arg_valid && arg.callback != nullptr)
						arg.callback (list);
					else if (arg_valid && arg.data != nullptr)
						*((ListOfStrings *)arg.data) = std::move (list);

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
				case type_string:
					arg_valid = ReadString (++n, *((std::wstring *)arg.data));
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
				search_file_found = true;
				de.AddIncludeFiles (list);
			}
		}

		if (!arg_valid)
			return usage_foo (L"invalid argument", false);
	}

	if (((size_t)opts.group_ext + (size_t)opts.group_hash + (size_t)opts.group_size) > 1)
		return usage_foo (L"Use only one group parameter", false);

	if (!scan_dir_found)
	{
		de.SetScanDirectories ({ std::filesystem::current_path () });
	}
	if (!search_file_found)
	{
		ListOfStrings s { L"*" };
		de.AddIncludeFiles (s);
	}
	if (!opts.hashes.empty ())
	{
		std::transform (opts.hashes.begin (), opts.hashes.end (), opts.hashes.begin (),
			[](std::wstring & buff)
		{
			std::transform (buff.begin (), buff.end (), buff.begin (),
				[](wchar_t c)
				{
					return std::towupper (c);
				}
			);
			return buff;
		});
		opts.print_hash = true;
	}
	if (!content.empty ())
	{
		int len = WideCharToMultiByte (CP_UTF8, 0, content.c_str (), -1, NULL, 0, NULL, NULL);
		std::vector <unsigned char> utf8 (len + 1);
		WideCharToMultiByte (CP_UTF8, 0, content.c_str (), -1, (char *)&utf8[0], len, NULL, NULL);
		opts.content = utf8.data ();
	}

	de.SetFileLimit (min_size, max_size);

	opts.filtering = !opts.hashes.empty () || !opts.content.empty ();
	opts.grouping = opts.group_ext || opts.group_hash || opts.group_size;
	opts.calc_hash = opts.print_hash || !opts.hashes.empty () || opts.group_hash;

	return true;
}

std::wstring FormatError (DWORD error)
{
	void * buff = nullptr;
	FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buff, 0, nullptr);
	std::unique_ptr <void, std::function <void (void *)>> pp (buff, [](void * buff) { LocalFree (buff); });
	return (wchar_t *)buff;
};

void DirEnumHandler::OnGivenPathFail (const std::wstring & file, std::wstring error)
{
	_usage (file + L": " + error, false);
}

void DirEnumHandler::OnFileFound (std::filesystem::path && file, uintmax_t size)
{
	_files.emplace_back (std::move (file), size);
	_file_found_callback (&(_files.back ()));
}

void DirEnumHandler::OnFileFound (const std::filesystem::path & file, uintmax_t size)
{
	_files.emplace_back (std::filesystem::path (file), size);
	_file_found_callback (&(_files.back ()));
}

void DirEnumHandler::OnScanError (const std::string & error)
{
	printf ("%s\n", error.c_str ());
}

std::wstring HashToString (File * file)
{
	if (file->Failed ())
		return L"Hash fail: " + FormatError (file->Error ());
	return file->Hash ();
};

void SimplePrint (const Opt & opts, File & file)
{
	if (opts.print_hash)
	{
		auto hash = HashToString (&file);
		if (opts.print_size)
			wprintf (L"%s [%s] [%s]\n", file.Path (), hash.c_str (), file.SizeFormatted ().c_str ());
		else
			wprintf (L"%s [%s]\n", file.Path (), hash.c_str ());
	}
	else
	{
		wprintf (L"%s", file.Path ());
		if (opts.print_size)
			wprintf (L" [%s]", file.SizeFormatted ().c_str ());
		wprintf (L"\n");
	}
}

void PrintGrouped (const Opt & opts, ListOfFiles & files)
{
	if (opts.group_size)
	{
		std::map <uintmax_t, std::list <File *>> group;
		for (auto & file : files)
		{
			group[file.Size ()].push_back (&file);
		}

		for (auto & g : group)
		{
			auto pfile = *g.second.begin ();
			wprintf (L"[%s]\n", pfile->SizeFormatted ().c_str ());
			for (auto file : g.second)
			{
				wprintf (L"%s", file->Path ());
				if (opts.print_hash)
					wprintf (L" [%s]", HashToString (file).c_str ());
				wprintf (L"\n");
			}
		}
	}
	else if (opts.group_ext)
	{
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
			wprintf (L"[%s]\n", g.first.c_str ());

			for (auto file : g.second)
			{
				if (opts.print_hash)
				{
					auto hash = HashToString (file);
					if (opts.print_size)
						wprintf (L"%s [%s] [%s]\n", file->Path (), hash.c_str (), file->SizeFormatted ().c_str ());
					else
						wprintf (L"%s [%s]\n", file->Path (), hash.c_str ());
				}
				else
				{
					wprintf (L"%s", file->Path ());
					if (opts.print_size)
						wprintf (L" [%s]", file->SizeFormatted ().c_str ());
					wprintf (L"\n");
				}
			}
		}
	}
	else if (opts.group_hash)
	{
		std::map <std::wstring, std::list <File *>> group;
		for (auto & file : files)
		{
			auto ext = file.Ext ();
			group[file.Hash ()].push_back (&file);
		}

		for (auto & g : group)
		{
			wprintf (L"[%s]\n", g.first.c_str ());
			for (auto file : g.second)
			{
				wprintf (L"%s", file->Path ());
				if (opts.print_size)
					wprintf (L" [%s]", file->SizeFormatted ().c_str ());
				wprintf (L"\n");
			}
		}
	}
}


#include "pch.h"
#include "direnum.h"


DirEnumerator::DirEnumerator (IDirEnumHandler * handler) :
	m_pHandler (handler)
{
}

bool DirEnumerator::SetScanDirectories (const ListOfStrings & list)
{
	for (const auto & _l : list)
	{
		try
		{
			std::filesystem::path path (_l);

			if (path.is_relative ())
			{
				m_dir_pathes.emplace_back (std::filesystem::absolute (path));
			}
			else
			{
				m_dir_pathes.push_back (std::move (path));
			}

			if (!std::filesystem::exists (m_dir_pathes.back ()))
			{
				if (m_pHandler != nullptr)
					m_pHandler->OnGivenPathFail (_l, L"Path not found");
				return false;
			}

			if (!std::filesystem::is_directory (m_dir_pathes.back ()))
			{
				m_file_pathes.push_back (std::move (m_dir_pathes.back ()));
				m_dir_pathes.pop_back ();
			}
		}
		catch (...)
		{
			if (m_pHandler != nullptr)
				m_pHandler->OnGivenPathFail (_l, L"Path is invalid");
			return false;
		}
	}

	return true;
}

void DirEnumerator::AddFileList (ListOfStrings & list, ListOfMasks & add_to)
{
	std::transform (list.begin (), list.end (), list.begin (), 
		[] (std::wstring & buff)
	{
		std::transform (buff.begin (), buff.end (), buff.begin (),
			[](wchar_t c)
			{
				return std::towlower (c);
			}
		);
		return buff;
	});

	for (auto & mask : list)
	{
		if (mask.find_first_of (L"?*") == std::wstring::npos)
		{
			add_to.push_back ({ mask, false });
		}
		else
		{
			std::wstring masked (L"^");
			for (auto c : mask)
			{
				switch (c)
				{
				case L'*':
					masked += L".*";
					break;
				case L'?':
					masked += L'.';
					break;

				case L'(':
				case L')':
				case L'+':
				case L'[':
				case L']':
				case L'{':
				case L'}':
				case L'\\':
				case L'$':
				case L'^':
					masked += L'\\' + std::wstring (1, c);
					break;

				default:
					masked += c;
				}
			}
			masked += L'$';
			add_to.push_back ({ std::move (masked), true });
		}
	}
}

void DirEnumerator::AddExcludeDirectories (ListOfStrings & list)
{
	AddFileList (list, m_exc_dir_mask);
}

void DirEnumerator::AddExcludeFiles (ListOfStrings & list)
{
	AddFileList (list, m_exc_file_mask);
}

void DirEnumerator::AddExcludePaths (ListOfStrings & list)
{
	AddFileList (list, m_exc_path_mask);
}

void DirEnumerator::AddIncludeDirectories (ListOfStrings & list)
{
	AddFileList (list, m_inc_dir_mask);
}

void DirEnumerator::AddIncludeFiles (ListOfStrings & list)
{
	AddFileList (list, m_inc_file_mask);
}

void DirEnumerator::AddIncludePaths (ListOfStrings & list)
{
	AddFileList (list, m_inc_path_mask);
}

void DirEnumerator::SetFileLimit (uintmax_t minsize /*= 0*/, uintmax_t maxsize /*= (uintmax_t)-1*/)
{
	m_min_size = minsize;
	m_max_size = maxsize;
}

bool DirEnumerator::MatchMask (const Mask & mask, const std::wstring & str)
{
	try
	{
		if (mask.use_regex)
		{
			std::wregex r (mask.mask, std::wregex::ECMAScript);
			std::wsmatch m;
			return std::regex_search (str, m, r);
		}
		return mask.mask == str;
	}
	catch (...) 
	{
		throw EnumException { "Invalid search mask" };
	}
	return false;
}

bool DirEnumerator::IsObjectIgnored (std::wstring & obj, uintmax_t filesize, ObjType type, bool scan_dir_includes)
{
	if (ObjType::file == type && m_min_size > 0 && filesize < m_min_size)
		return true;
	if (ObjType::file == type && m_max_size != (uintmax_t)-1 && filesize > m_max_size)
		return true;

	if (!m_opt_file && ObjType::file == type || !m_opt_dir && ObjType::directory == type || !m_opt_path && ObjType::path == type)
		return false;


	auto MakeLower = [](std::wstring & buff)
	{
		std::transform (buff.begin (), buff.end (), buff.begin (),
			[](wchar_t c)
			{
				return std::towlower (c);
			}
		);
	};
	MakeLower (obj);

	struct
	{
		ListOfMasks * mask;
		bool exclude_mask;
		ObjType type;
	}
	mask [] = 
	{
		{ scan_dir_includes ? nullptr : &m_exc_file_mask, true, ObjType::file },
		{ scan_dir_includes ? nullptr : &m_exc_dir_mask, true, ObjType::directory },
		{ scan_dir_includes ? nullptr : &m_exc_path_mask, true, ObjType::path },

		{ scan_dir_includes ? nullptr : &m_inc_file_mask, false, ObjType::file },
		{ scan_dir_includes ? &m_inc_dir_mask : nullptr, false, ObjType::directory },
		{ scan_dir_includes ? &m_inc_path_mask : nullptr, false, ObjType::path }
	};

	static std::map <std::wstring, bool> ignored_inc, ignored_exc;
	std::map <std::wstring, bool> * ignored = (scan_dir_includes ? &ignored_inc : &ignored_exc);

	auto processed = ignored->find (obj);
	if (processed != ignored->end ())
		return processed->second;

	bool checked = false;

	for (auto & m : mask)
	{
		if (nullptr == m.mask)
			continue;

		if (m.type == type)
		{
			checked = true;
			bool match_found = false;

			for (auto & mask_item : *m.mask)
			{
				if (match_found = MatchMask (mask_item, obj))
					break;
			}

			if (m.exclude_mask && match_found)
			{
				(*ignored)[obj] = true;
				return true;
			}
			if (!m.exclude_mask && !match_found && !m.mask->empty ())
			{
				(*ignored)[obj] = true;
				return true;
			}
		}
	}

	if (checked)
		(*ignored)[obj] = false;

	return false;
}

void DirEnumerator::EnumerateDirectory_Win7 (const std::filesystem::path & root)
{
	ListOfPaths dirs;
	dirs.push_back (root);

	do
	{
		auto dir = dirs.begin ();

		try
		{
			for (auto & path : std::filesystem::directory_iterator (*dir))
			{
				bool is_dir = std::filesystem::is_directory (path);
				uintmax_t size = path.file_size ();

				auto ps = path.path ().wstring ();
				auto fns = path.path ().filename ().wstring ();

				if (IsObjectIgnored (ps, 0, ObjType::path, false) ||
					IsObjectIgnored (fns, size, is_dir ? ObjType::directory : ObjType::file, false))
				{
					continue;
				}

				if (is_dir)
				{
					if (!IsObjectIgnored (ps, 0, ObjType::path, true) &&
						!IsObjectIgnored (fns, 0, ObjType::directory, true))
					{
						m_pHandler->OnDirFound (path.path ());
					}
					dirs.push_back (path.path ());
				}
				else
				{
					auto parent = path.path ().parent_path ();
					if (!IsObjectIgnored (parent.wstring (), 0, ObjType::path, true) &&
						!IsObjectIgnored (parent.filename ().wstring (), 0, ObjType::directory, true))
					{
						m_pHandler->OnFileFound (path.path (), size);
					}
				}
			}
		}
		catch (const std::exception & ex)
		{
			m_pHandler->OnScanError (ex.what ());
		}

		dirs.pop_front ();
	} 
	while (!dirs.empty ());
}

void DirEnumerator::EnumerateDirectory_WinXp (const std::filesystem::path & root)
{
	ListOfPaths dirs;
	dirs.push_back (root);

	WIN32_FIND_DATA fd = {};

	do
	{
		auto dir = dirs.begin ();
		(*dir) /= L"*.*";

		HANDLE hfind = ::FindFirstFile (dir->c_str (), &fd);
		if (hfind != INVALID_HANDLE_VALUE)
		{
			std::unique_ptr <void, decltype (&FindClose)> h (hfind, FindClose);
			dir->remove_filename ();

			do
			{
				if (wcscmp (fd.cFileName, L".") == 0 || wcscmp (fd.cFileName, L"..") == 0)
					continue;

				uintmax_t size = (fd.nFileSizeHigh * (MAXDWORD + 1I64)) + fd.nFileSizeLow;
				bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;

				std::filesystem::path path (*dir);
				path /= fd.cFileName;

				if (IsObjectIgnored (path.wstring (), 0, ObjType::path, false) ||
					IsObjectIgnored (path.filename ().wstring (), size, is_dir ? ObjType::directory : ObjType::file, false))
				{
					continue;
				}

				if (is_dir)
				{
					m_pHandler->OnDirFound (path);
					dirs.push_back (std::move (path));
				}
				else
				{
					m_pHandler->OnFileFound (std::move (path), size);
				}
			} 
			while (::FindNextFile (hfind, &fd));
		}

		dirs.pop_front ();
	} 
	while (!dirs.empty ());
}

void DirEnumerator::EnumerateDirectory (const std::filesystem::path & root)
{
	if (IsWindows7OrGreater ())
		return EnumerateDirectory_Win7 (root);

	// std::filesystem::[recursive]directory_iterator will throw an exception on WinXP
	return EnumerateDirectory_WinXp (root);
}

void DirEnumerator::EnumerateDirectory ()
{
	if (nullptr == m_pHandler)
		return;

	m_opt_file = !m_exc_file_mask.empty () || !m_inc_file_mask.empty ();
	m_opt_dir = !m_exc_dir_mask.empty () || !m_inc_dir_mask.empty ();
	m_opt_path = !m_exc_path_mask.empty () || !m_inc_path_mask.empty ();

	for (auto & file : m_file_pathes)
	{
		uintmax_t size = std::filesystem::file_size (file);
		m_pHandler->OnFileFound (std::move (file), size);
	}

	for (auto & dir : m_dir_pathes)
	{
		EnumerateDirectory (dir);
	}
}

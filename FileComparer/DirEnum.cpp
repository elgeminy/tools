
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

void DirEnumerator::AddFileList (ListOfStrings & list, ListOfStrings & add_to)
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
		std::wstring basic = L"^";
		for (auto c : mask)
		{
			if (std::iswalnum (c))
				basic += std::wstring (1, c);
			else
			{
				switch (c)
				{
				case L'*':
					basic += L".*";
					break;
				case L'?':
					basic += L".";
					break;
				case L'.':
					basic += L"\\.";
					break;
				default:
					basic += L"\\" + std::wstring (1, c);
				}
			}
		}
		basic += L"$";
		add_to.push_back (std::move (basic));
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

bool DirEnumerator::MatchMask (const std::wstring & mask, const std::wstring & str)
{
	try
	{
		std::wregex r (mask, std::wregex::basic);
		std::wsmatch m;
		return std::regex_search (str, m, r);
	}
	catch (...) 
	{
		throw EnumException { "Invalid search mask" };
	}
	return false;
}

bool DirEnumerator::IsPathIgnored (std::wstring path)
{
	if (!m_opt_path)
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
	MakeLower (path);

	struct
	{
		ListOfStrings & mask;
		bool exclude_mask;
	}
	mask[] =
	{
		{ m_exc_path_mask, true },
		{ m_inc_path_mask, false }
	};

	for (auto & m : mask)
	{
		bool match_found = false;

		for (auto & mask_item : m.mask)
		{
			if (match_found = MatchMask (mask_item, path))
				break;
		}

		if (m.exclude_mask && match_found)
			return true;
		if (!m.exclude_mask && !match_found && !m.mask.empty ())
			return true;
	}

	return false;
}

bool DirEnumerator::IsObjectIgnored (std::wstring obj, bool is_dir, uintmax_t filesize)
{
	if (!is_dir && m_min_size > 0 && filesize < m_min_size)
		return true;
	if (!is_dir && m_max_size != (uintmax_t)-1 && filesize > m_max_size)
		return true;

	if (!m_opt_file && !is_dir || !m_opt_dir && is_dir)
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
		ListOfStrings & mask;
		bool exclude_mask;
		bool for_dirs;
	}
	mask [] = 
	{
		{ m_exc_file_mask, true, false },
		{ m_exc_dir_mask, true, true },
		{ m_inc_file_mask, false, false },
		{ m_inc_dir_mask, false, true },
	};

	for (auto & m : mask)
	{
		if (is_dir && m.for_dirs || !is_dir && !m.for_dirs)
		{
			bool match_found = false;

			for (auto & mask_item : m.mask)
			{
				if (match_found = MatchMask (mask_item, obj))
					break;
			}

			if (m.exclude_mask && match_found)
				return true;
			if (!m.exclude_mask && !match_found && !m.mask.empty ())
				return true;
		}
	}

	return false;
}

void DirEnumerator::EnumerateDirectory_Win7_Rec (const std::filesystem::path & root)
{
	try
	{
		for (auto & obj : std::filesystem::recursive_directory_iterator (root))
		{
			uintmax_t size = obj.file_size ();
			bool is_dir = std::filesystem::is_directory (obj);

			if (IsObjectIgnored (obj.path ().filename ().wstring (), is_dir, size))
			{
				m_pHandler->OnFileIgnored (obj.path ());
				continue;
			}

			if (!is_dir && !IsPathIgnored (obj.path().wstring ()))
				m_pHandler->OnFileFound (obj.path (), size);
		}
	}
	catch (const std::exception & ex)
	{
		throw EnumException { ex.what () };
	}
}

void DirEnumerator::EnumerateDirectory_Win7 (const std::filesystem::path & root)
{
	/*
	will throw an exception if no access to one of the folders

	if (!m_opt_dir)
		return EnumerateDirectory_Win7_Rec (root);
	*/

	ListOfPaths dirs;
	dirs.push_back (root);

	do
	{
		auto dir = dirs.begin ();

		try
		{
			for (auto & obj : std::filesystem::directory_iterator (*dir))
			{
				bool is_dir = std::filesystem::is_directory (obj);
				uintmax_t size = obj.file_size ();

				if (IsObjectIgnored (obj.path ().filename ().wstring (), is_dir, size))
				{
					m_pHandler->OnFileIgnored (obj.path ());
					continue;
				}

				if (is_dir)
				{
					dirs.push_back (obj.path ());
				}
				else if (!IsPathIgnored (obj.path ().wstring ()))
					m_pHandler->OnFileFound (obj.path (), size);
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

				std::filesystem::path obj (*dir);
				obj /= fd.cFileName;

				if (IsObjectIgnored (fd.cFileName, is_dir, size))
				{
					m_pHandler->OnFileIgnored (std::move (obj));
					continue;
				}

				if (is_dir)
				{
					dirs.push_back (std::move (obj));
				}
				else
				{
					m_pHandler->OnFileFound (std::move (obj), size);
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

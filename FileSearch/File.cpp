// FileComparer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "File.h"
#include "sha1.h"

uintmax_t File::m_lMaxHeapSize = 1024 * 1024;

File::File (std::filesystem::path && path, uintmax_t size) :
	m_filepath (std::move (path)),
	m_size (size),
	m_pmap (nullptr, UnmapViewOfFile),
	m_hmap (nullptr, CloseHandle),
	m_hfile (nullptr, CloseHandle)
{
}

File::File (std::filesystem::path && path) :
	File (std::move (path), 0)
{
}

File::~File ()
{
	CloseFile ();
}

void File::CloseFile () noexcept
{
	m_pmap.reset ();
	m_hmap.reset ();
	m_hfile.reset ();
	m_buffer.clear ();
}

bool File::OpenFile () noexcept
{
	auto StoreErrorAndExit = [&](DWORD error = ::GetLastError ())
	{
		m_error = error;
		return false;
	};

	HANDLE h = ::CreateFile (m_filepath.c_str (), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (INVALID_HANDLE_VALUE == h)
		return StoreErrorAndExit ();
	FileHandle hfile (h, CloseHandle);

	LARGE_INTEGER size = {};
	if (!::GetFileSizeEx (hfile.get(), &size))
		return StoreErrorAndExit ();
	m_size = size.QuadPart;

	try
	{
		if (m_size <= m_lMaxHeapSize)
		{
			m_buffer.resize (static_cast <size_t> (m_size));
			DWORD read = 0;
			if (!::ReadFile (hfile.get (), m_buffer.data (), static_cast <DWORD> (m_size), &read, nullptr))
				return StoreErrorAndExit ();
			return true;
		}
	}
	catch (...)
	{
		return StoreErrorAndExit (ERROR_NOT_ENOUGH_MEMORY);
	}

	h = ::CreateFileMapping (hfile.get (), nullptr, PAGE_READONLY | SEC_COMMIT, 0, 0, nullptr);
	if (nullptr == h)
		return StoreErrorAndExit ();
	FileHandle hmap (h, CloseHandle);

	void * pview = ::MapViewOfFile (hmap.get(), FILE_MAP_READ, 0, 0, 0);
	if (nullptr == pview)
		return StoreErrorAndExit ();

	m_pmap.reset ((unsigned char *)pview);
	m_hmap = std::move (hmap);
	m_hfile = std::move (hfile);
	return true;
}

const unsigned char * File::FilePtr () const noexcept
{
	if (m_buffer.empty () && nullptr == m_pmap)
		return nullptr;

	if (m_pmap != nullptr)
		return m_pmap.get();

	return m_buffer.data ();
}

bool File::CalcHash () noexcept
{
	if (!m_hash.empty ())
		return true;

	if (!OpenFile ())
		return false;

	auto ptr = FilePtr ();
	if (nullptr == ptr)
	{
		m_error = ERROR_INVALID_HANDLE;
		return false;
	}

	Sha1 sha1;
	sha1.ComputeHash (ptr, m_size);

	auto hash = sha1.GetReport ();
	int len = MultiByteToWideChar (CP_UTF8, 0, hash.c_str (), -1, NULL, 0);
	std::vector <wchar_t> uni (len + 1);
	MultiByteToWideChar (CP_UTF8, 0, hash.c_str (), -1, &uni[0], len);
	m_hash = uni.data ();

	CloseFile ();
	return true;
}

bool File::CompareTo (File & obj) noexcept
{
	auto Compare = [](File & f1, File & f2)
	{
		// recheck file size
		if (f1.m_size != f2.m_size)
			return false;

		auto ptr1 = f1.FilePtr ();
		auto ptr2 = f2.FilePtr ();

		if (nullptr == ptr1 || nullptr == ptr2)
		{
			f1.m_error = (nullptr == ptr1 ? ERROR_INVALID_HANDLE : NO_ERROR);
			f2.m_error = (nullptr == ptr2 ? ERROR_INVALID_HANDLE : NO_ERROR);
			return false;
		}

		uintmax_t it = 0;
		bool equal = true;
		do
		{
			if (*ptr1++ != *ptr2++)
			{
				equal = false;
				break;
			}
		} 
		while (++it < f1.m_size);

		return equal;
	};

	bool equal = false;
	if (OpenFile () && obj.OpenFile ())
	{
		equal = Compare (*this, obj);
	}

	CloseFile ();
	obj.CloseFile ();

	return equal;
}

bool File::MatchFilter (const std::list <std::wstring> & hashes, const std::basic_string <unsigned char> & content) noexcept
{
	if (!content.empty ())
	{
		if (!OpenFile ())
			return (m_filtering_result = false);

		std::basic_string_view <unsigned char> buff (FilePtr (), Size ());
		auto res = buff.find (content.c_str (), 0);
		m_filtering_result = (res != std::basic_string_view <unsigned char>::npos);

		CloseFile ();
	}

	if (m_filtering_result && !hashes.empty ())
	{
		if (!CalcHash ())
			return (m_filtering_result = false);

		auto res = std::find (hashes.begin (), hashes.end (), Hash ());
		m_filtering_result = (res != hashes.end ());
	}

	return m_filtering_result;
}

std::wstring File::SizeFormatted () const noexcept
{
	std::vector <wchar_t> fmt (101);
	StrFormatByteSize64 (Size (), fmt.data (), 100);
	return fmt.data ();
}











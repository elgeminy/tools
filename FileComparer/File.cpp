// FileComparer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "File.h"
#include "sha1.h"

uintmax_t File::m_lMaxHeapSize = 1024 * 1024;

File::File (std::filesystem::path && path) :
	m_filepath (std::move (path)), 
	m_pmap (nullptr, UnmapViewOfFile),
	m_hmap (nullptr, CloseHandle),
	m_hfile (nullptr, CloseHandle)
{
}

File::File (const std::filesystem::path & path) :
	m_filepath (path),
	m_pmap (nullptr, UnmapViewOfFile),
	m_hmap (nullptr, CloseHandle),
	m_hfile (nullptr, CloseHandle)
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
	auto StoreErrorAndExit = [&]()
	{
		m_error = ::GetLastError ();
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
		m_error = ERROR_NOT_ENOUGH_MEMORY;
		return false;
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
	m_hash = sha1.GetReport ();

	// do not keep open file' handle
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

	// do not keep open files' handle
	CloseFile ();
	obj.CloseFile ();

	return equal;
}
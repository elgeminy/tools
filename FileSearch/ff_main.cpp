
#include "pch.h"
#include "filefinder.h"

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
			L"  [<file_1> ... <file_N>] [" ARG_SCAN_PATH L"]\n" 
				L"\t[" ARG_CONTENT "]\n"
				L"\t[" ARG_HASH "]\n"
				L"\t[" ARG_MIN_SIZE L"] [" ARG_MAX_SIZE L"]\n" 
				L"\t[" ARG_EXC_FILES L"] [" ARG_INC_PATHS L"] [" ARG_EXC_PATHS L"] [" ARG_INC_DIRS L"] [" ARG_EXC_DIRS L"]\n"
				L"\t[" ARG_PRINT_HASH L"] [" ARG_PRINT_SIZE L"]\n"
				L"\t[" ARG_GROUP_EXT L"] [" ARG_GROUP_HASH L"] [" ARG_GROUP_SIZE L"]\n"
			L"\n"

			L"  <file_1...N>\t\t\t- list of file's masks to search (symbols (*) and (?) are allowed).\t\t\t\t\t\t\t\tOmit to find all files (e.g. '*')\n"

			L"  " ARG_SCAN_PATH L" <dir_path>\t\t\t- path to a directory to search in. Currect dir used by default\n"

			L"  " ARG_CONTENT L" <request>\t\t\t- data to search in file content\n"
	
			L"  " ARG_MIN_SIZE L" <file_size>\t\t- min file size to search\n"
			L"  " ARG_MAX_SIZE L" <file_size>\t\t- max file size to search\n"

			L"  " ARG_HASH L" <SHA1_hash_1...N>\t- list of file's SHA1 hashes to search\n"

			L"  " ARG_EXC_FILES L" <mask_1...N>\t\t- list of files' masks to skip from searching (symbols (*) and (?) are allowed)\n"

			L"  " ARG_INC_PATHS L" <mask_1...N>\t\t- list of path' masks to search in (symbols (*) and (?) are allowed)\n"
			L"  " ARG_EXC_PATHS L" <mask_1...N>\t\t- list of paths' masks to skip from searching (symbols (*) and (?) are allowed)\n"

			L"  " ARG_INC_DIRS L" <mask_1...N>\t\t- list of dirs' masks to search in (symbols (*) and (?) are allowed)\n"
			L"  " ARG_EXC_DIRS L" <mask_1...N>\t\t- list of dirs' masks to skip from searching (symbols (*) and (?) are allowed)\n"

			L"  " ARG_PRINT_HASH L"\t\t\t\t- print found file' SHA1 hash\n"
			L"  " ARG_PRINT_SIZE L"\t\t\t\t- print found file' size\n"

			L"  " ARG_GROUP_EXT L"\t\t\t\t- print found files grouping by extension\n"
			L"  " ARG_GROUP_HASH L"\t\t\t\t- print found files grouping by SHA1 hash\n"
			L"  " ARG_GROUP_SIZE L"\t\t\t\t- print found files grouping by size\n"
		L"\n"
	;

	return false;
}

int main ()
{
	::_wsetlocale (LC_CTYPE, L"");

	try
	{
		auto t_start = std::chrono::high_resolution_clock::now ();

		Opt opts;
		std::list <std::future <File *>> fut_waiters;
		std::mutex fut_mutex, con_mutex;

		auto CalcHash = [&opts, &con_mutex](File * file, std::promise <File *> && p)
		{
			if (opts.filtering)
				file->MatchFilter (opts.hashes, opts.content);

			if (opts.calc_hash)
				file->CalcHash ();

			if (!opts.grouping && file->FilteringResult ())
			{
				std::lock_guard <std::mutex> lk (con_mutex);
				SimplePrint (opts, *file);
			}

			p.set_value (file);
		};

		auto PrintFile = [&opts, &fut_waiters, &fut_mutex, hash_func = CalcHash](File * file)
		{
			// just print and exit
			if (!opts.filtering && !opts.grouping && !opts.calc_hash)
			{
				SimplePrint (opts, *file);
				return;
			}

			if (opts.filtering || opts.calc_hash)
			{
				// calculate hash and decide later
				std::promise <File *> p;
				{
					std::lock_guard <std::mutex> lk (fut_mutex);
					fut_waiters.push_back (p.get_future ());

					//std::async (std::launch::async, hash_func, file, std::move (p));	// 17933780000
					std::async (hash_func, file, std::move (p));						// 18523340100
					//std::thread (hash_func, file, std::move (p)).detach ();			//  5087526600
				}
				return;
			}
		};

		DirEnumHandler handler (PrintFile, Usage);
		DirEnumerator de (&handler);
		if (!ReadArg (de, opts, Usage))
			return 0;
		de.EnumerateDirectory ();

		bool wait_futures = false;
		{
			std::lock_guard <std::mutex> lk (fut_mutex);
			wait_futures = !fut_waiters.empty ();
		}
		if (wait_futures)
		{
			for (auto & fut : fut_waiters)
			{
				fut.wait ();
			}
		}

		if (opts.grouping)
		{
			if (opts.filtering)
			{
				auto r = std::remove_if (handler._files.begin (), handler._files.end (),
					[](const File & file)
					{
						return !file.FilteringResult ();
					}
				);
				handler._files.erase (r, handler._files.end ());
			}
			PrintGrouped (opts, handler._files);
		}

		std::wcout << L"\n" << handler._files.size () << L" files found\n";

		auto t_end = std::chrono::high_resolution_clock::now ();
		auto t_elapsed = t_end - t_start;
		auto t_seconds = t_elapsed.count () / 1'000'000'000;
		std::cout << "Elapsed time: " << t_seconds << " seconds\n";
	}
	catch (EnumException & ex)
	{
		std::cout << "Unexpected error occuped: " << ex.error << std::endl;
	}
	catch (std::exception & ex)
	{
		std::cout << "Unexpected error occuped: " << ex.what () << std::endl;
	}
	catch (...)
	{
		std::wcout << L"Unexpected error occuped" << std::endl;
	}

	return 0;
}

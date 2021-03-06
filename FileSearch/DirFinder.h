
#pragma once
#include "pch.h"
#include "direnum.h"

#define ARG_PREFIX		L'-'
#define ARG_USAGE		L"-?"

#define ARG_SCAN_PATH	L"-p"

#define ARG_EXC_DIRS	L"-ed"

#define ARG_INC_PATHS	L"-ip"
#define ARG_EXC_PATHS	L"-ep"

using UsageFunc = std::function <bool (std::wstring, bool)>;

bool ReadArg (DirEnumerator & de, UsageFunc usage_foo);

struct DirEnumHandler : public IDirEnumHandler
{
	DirEnumHandler (std::function <void (const std::filesystem::path &)> fc, UsageFunc usage) :
		_dir_found_callback (fc),
		_usage (usage)
	{}

	void OnGivenPathFail (const std::wstring & file, std::wstring error);

	void OnFileFound (std::filesystem::path && file, uintmax_t size) {}
	void OnFileFound (const std::filesystem::path & file, uintmax_t size) {}

	void OnDirFound (const std::filesystem::path & dir);

	void OnScanError (const std::string & error);


	std::function <void (const std::filesystem::path &)> _dir_found_callback;
	UsageFunc _usage;
	ListOfPaths _dirs;
};


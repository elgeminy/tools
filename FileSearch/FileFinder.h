
#pragma once
#include "pch.h"
#include "direnum.h"
#include "file.h"

#define ARG_PREFIX		L'-'
#define ARG_USAGE		L"-?"

#define ARG_SCAN_PATH	L"-p"

#define ARG_EXC_FILES	L"-ef"

#define ARG_INC_DIRS	L"-id"
#define ARG_EXC_DIRS	L"-ed"

#define ARG_INC_PATHS	L"-ip"
#define ARG_EXC_PATHS	L"-ep"

#define ARG_MIN_SIZE	L"-min"
#define ARG_MAX_SIZE	L"-max"

#define ARG_HASH		L"-hash"

#define ARG_CONTENT		L"-c"

#define ARG_PRINT_HASH	L"-psh"
#define ARG_PRINT_SIZE	L"-psz"

#define ARG_GROUP_EXT	L"-gex"
#define ARG_GROUP_SIZE	L"-gsz"
#define ARG_GROUP_HASH	L"-gsh"

using UsageFunc = std::function <bool (std::wstring, bool)>;

struct Opt
{
	ListOfStrings hashes;
	std::basic_string <unsigned char> content;
	bool print_hash = false;
	bool print_size = false;
	bool group_size = false;
	bool group_ext = false;
	bool group_hash = false;

	bool filtering = false;
	bool grouping = false;
	bool calc_hash = false;
};

bool ReadArg (DirEnumerator & de, Opt & opts, UsageFunc usage_foo);
void PrintGrouped (const Opt & opts, ListOfFiles & files);
void SimplePrint (const Opt & opts, File & file);

struct DirEnumHandler : public IDirEnumHandler
{
	DirEnumHandler (std::function <void (File *)> fc, UsageFunc usage) :
		_file_found_callback (fc),
		_usage (usage)
	{}

	void OnGivenPathFail (const std::wstring & file, std::wstring error);

	void OnFileFound (std::filesystem::path && file, uintmax_t size);
	void OnFileFound (const std::filesystem::path & file, uintmax_t size);

	void OnDirFound (const std::filesystem::path & dir) {}

	void OnScanError (const std::string & error);


	std::function <void (File *)> _file_found_callback;
	UsageFunc _usage;
	ListOfFiles _files;
};


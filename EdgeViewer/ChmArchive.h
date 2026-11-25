#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct mschm_decompressor;
struct mschmd_header;
struct mschmd_file;

//------------------------------------------------------------------------
// "/#SYSTEM": code 0 = contents file, 2 = default topic, 4 = LCID,
// 5 = default window. Paths get a leading '/' and '/' separators.
struct ChmSystemInfo
{
	std::string toc;
	std::string start;
	std::string defaultWindow;
	unsigned lcid = 0;
};
ChmSystemInfo ParseChmSystem(const std::vector<uint8_t>& sys);

// "/#WINDOWS" with strings in "/#STRINGS": the entry named like the default
// window, else the first one.
struct ChmWindowInfo
{
	std::string toc;        // 0x60
	std::string start;      // 0x68
	std::string home;       // 0x6C
	bool navClosed = false; // 0x74
};
ChmWindowInfo ParseChmWindows(const std::vector<uint8_t>& windows, const std::vector<uint8_t>& strings,
                              const std::string& defaultWindow);

// Charset for the Content-Type of a help page, or "" (the page decides):
// BOM or declared charset -> "", valid UTF-8 -> "utf-8", else the language
// code page. ASCII-only pages -> "".
std::string ChmPageCharset(const std::vector<uint8_t>& page, const std::string& languageCharset);

// evh://chm.example/<path>[?query][#fragment] -> path (percent-escaped, no
// leading '/') and query.
bool ParseChmUri(const std::wstring& uri, std::wstring& escapedPath, std::wstring& query);

// ANSI code page of an LCID as a charset label ("windows-1251"), "" if unknown.
std::string ChmCodePageLabel(unsigned lcid);

// The CHM view and its pages: evh://chm.example/...
inline bool IsChmUri(std::wstring_view uri) { return uri.starts_with(L"evh://chm.example/"); }

// Quoted JS string literal for ExecuteScript.
std::wstring ChmJsString(std::wstring_view s);

//------------------------------------------------------------------------
// Read-only CHM archive (libmspack). Entries are decompressed into memory on
// demand. Paths ("/dir/page.htm") are case-insensitive.
class ChmArchive
{
public:
	static std::shared_ptr<ChmArchive> Open(const std::filesystem::path& file);
	~ChmArchive();

	ChmArchive(const ChmArchive&) = delete;
	ChmArchive& operator=(const ChmArchive&) = delete;

	bool Read(const std::string& entryPath, std::vector<uint8_t>& out);

	// Archive paths, empty when not found.
	const std::string& StartTopic() const { return m_start; }
	const std::string& HomeTopic() const { return m_home; }
	const std::string& TocPath() const { return m_toc; }
	const std::string& CodePageLabel() const { return m_codePage; }
	bool NavClosed() const { return m_navClosed; }

private:
	ChmArchive() = default;
	const mschmd_file* Find(const std::string& entryPath) const;
	void ReadMetadata();

	std::string m_utf8Path; // libmspack keeps a pointer to this string
	mschm_decompressor* m_d = nullptr;
	mschmd_header* m_h = nullptr;
	std::map<std::string, const mschmd_file*> m_index; // lower-case path -> entry
	std::mutex m_mutex; // libmspack decompressor state is not thread-safe
	std::string m_start;
	std::string m_home;
	std::string m_toc;
	std::string m_codePage;
	bool m_navClosed = false;
};
//------------------------------------------------------------------------

#include "ChmArchive.h"
#include "Globals.h"
#include "third_party/libmspack/mspack/mspack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

//------------------------------------------------------------------------
namespace
{
constexpr off_t kMaxEntrySize = 64 * 1024 * 1024;

// mspack_system: READ opens the archive by a UTF-8 path; WRITE gets a
// std::vector<uint8_t>* as the "file name" and appends to it.
struct EvFile
{
	FILE* fh = nullptr;
	std::vector<uint8_t>* out = nullptr;
};

mspack_file* EvOpen(mspack_system*, const char* filename, int mode)
{
	if (mode == MSPACK_SYS_OPEN_READ)
	{
#ifdef _WIN32
		FILE* fh = _wfopen(to_utf16(filename).c_str(), L"rb");
#else
		FILE* fh = std::fopen(filename, "rb");
#endif
		if (!fh)
			return nullptr;
		auto* f = new EvFile;
		f->fh = fh;
		return reinterpret_cast<mspack_file*>(f);
	}
	if (mode == MSPACK_SYS_OPEN_WRITE)
	{
		auto* f = new EvFile;
		f->out = reinterpret_cast<std::vector<uint8_t>*>(const_cast<char*>(filename));
		return reinterpret_cast<mspack_file*>(f);
	}
	return nullptr;
}

void EvClose(mspack_file* file)
{
	auto* f = reinterpret_cast<EvFile*>(file);
	if (f->fh)
		std::fclose(f->fh);
	delete f;
}

int EvRead(mspack_file* file, void* buffer, int bytes)
{
	auto* f = reinterpret_cast<EvFile*>(file);
	const size_t n = std::fread(buffer, 1, static_cast<size_t>(bytes), f->fh);
	if (n == 0 && std::ferror(f->fh))
		return -1;
	return static_cast<int>(n);
}

int EvWrite(mspack_file* file, void* buffer, int bytes)
{
	auto* f = reinterpret_cast<EvFile*>(file);
	if (f->out->size() + static_cast<size_t>(bytes) > static_cast<size_t>(kMaxEntrySize))
		return -1;
	const auto* p = static_cast<const uint8_t*>(buffer);
	f->out->insert(f->out->end(), p, p + bytes);
	return bytes;
}

int EvSeek(mspack_file* file, off_t offset, int mode)
{
	auto* f = reinterpret_cast<EvFile*>(file);
	const int whence = mode == MSPACK_SYS_SEEK_CUR ? SEEK_CUR : mode == MSPACK_SYS_SEEK_END ? SEEK_END : SEEK_SET;
	return std::fseek(f->fh, static_cast<long>(offset), whence) == 0 ? 0 : -1;
}

off_t EvTell(mspack_file* file)
{
	return static_cast<off_t>(std::ftell(reinterpret_cast<EvFile*>(file)->fh));
}

void EvMessage(mspack_file*, const char*, ...) {}

void* EvAlloc(mspack_system*, size_t bytes) { return std::malloc(bytes); }
void EvFree(void* ptr) { std::free(ptr); }
void EvCopy(void* src, void* dest, size_t bytes) { std::memmove(dest, src, bytes); }

mspack_system g_evSystem = {
	&EvOpen, &EvClose, &EvRead, &EvWrite, &EvSeek, &EvTell,
	&EvMessage, &EvAlloc, &EvFree, &EvCopy, nullptr
};

std::string LowerAscii(std::string s)
{
	for (auto& c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

bool EndsWith(const std::string& s, const char* suffix)
{
	const size_t n = std::strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// NUL-terminated string -> archive path ("/dir/page.htm").
std::string SystemString(const uint8_t* p, size_t len)
{
	std::string s(reinterpret_cast<const char*>(p), len);
	if (const auto nul = s.find('\0'); nul != std::string::npos)
		s.resize(nul);
	for (auto& c : s)
		if (c == '\\')
			c = '/';
	if (!s.empty() && s[0] != '/')
		s.insert(s.begin(), '/');
	return s;
}
} // namespace
//------------------------------------------------------------------------
std::shared_ptr<ChmArchive> ChmArchive::Open(const std::filesystem::path& file)
{
	int selftest = MSPACK_ERR_OK;
	MSPACK_SYS_SELFTEST(selftest);
	if (selftest != MSPACK_ERR_OK)
		return nullptr;

	std::shared_ptr<ChmArchive> a(new ChmArchive);
	a->m_utf8Path = to_utf8(file.wstring());
	a->m_d = mspack_create_chm_decompressor(&g_evSystem);
	if (!a->m_d)
		return nullptr;
	a->m_h = a->m_d->open(a->m_d, a->m_utf8Path.c_str());
	if (!a->m_h)
		return nullptr;

	for (const mschmd_file* f = a->m_h->files; f; f = f->next)
		if (f->filename)
			a->m_index[LowerAscii(f->filename)] = f;
	if (a->m_index.empty())
		return nullptr;

	a->ReadMetadata();
	return a;
}
//------------------------------------------------------------------------
ChmArchive::~ChmArchive()
{
	if (m_d && m_h)
		m_d->close(m_d, m_h);
	if (m_d)
		mspack_destroy_chm_decompressor(m_d);
}
//------------------------------------------------------------------------
const mschmd_file* ChmArchive::Find(const std::string& entryPath) const
{
	const auto it = m_index.find(LowerAscii(entryPath));
	return it == m_index.end() ? nullptr : it->second;
}
//------------------------------------------------------------------------
bool ChmArchive::Read(const std::string& entryPath, std::vector<uint8_t>& out)
{
	out.clear();
	const mschmd_file* f = Find(entryPath);
	if (!f || f->length < 0 || f->length > kMaxEntrySize)
		return false;

	std::lock_guard lock(m_mutex);
	out.reserve(static_cast<size_t>(f->length));
	const int err = m_d->extract(m_d, const_cast<mschmd_file*>(f), reinterpret_cast<const char*>(&out));
	if (err != MSPACK_ERR_OK)
	{
		out.clear();
		return false;
	}
	return true;
}
//------------------------------------------------------------------------
void ChmArchive::ReadMetadata()
{
	std::vector<uint8_t> sysBytes, winBytes, strBytes;
	Read("/#SYSTEM", sysBytes);
	const ChmSystemInfo sys = ParseChmSystem(sysBytes);
	ChmWindowInfo win;
	if (Read("/#WINDOWS", winBytes) && Read("/#STRINGS", strBytes))
		win = ParseChmWindows(winBytes, strBytes, sys.defaultWindow);

	// A path may carry an anchor ("page.htm#top").
	const auto exists = [&](const std::string& p) { return !p.empty() && Find(p.substr(0, p.find('#'))); };
	const auto firstWith = [&](std::initializer_list<const char*> exts) -> std::string {
		for (const auto& [key, f] : m_index)
			for (const char* ext : exts)
				if (EndsWith(key, ext))
					return f->filename;
		return "";
	};

	m_toc = exists(sys.toc) ? sys.toc : exists(win.toc) ? win.toc : firstWith({ ".hhc" });

	if (exists(sys.start)) m_start = sys.start;
	else if (exists(win.start)) m_start = win.start;
	else
	{
		for (const char* name : { "/index.htm", "/index.html", "/default.htm", "/default.html" })
			if (const auto* f = Find(name)) { m_start = f->filename; break; }
		if (m_start.empty())
			m_start = firstWith({ ".htm", ".html" });
	}

	m_home = exists(win.home) ? win.home : m_start;
	m_codePage = ChmCodePageLabel(sys.lcid);
	m_navClosed = win.navClosed;
}
//------------------------------------------------------------------------
namespace
{
uint32_t Dword(const std::vector<uint8_t>& b, size_t pos)
{
	if (pos + 4 > b.size())
		return 0;
	return b[pos] | (b[pos + 1] << 8) | (b[pos + 2] << 16) | (static_cast<uint32_t>(b[pos + 3]) << 24);
}

// NUL-terminated string at `offset` in "/#STRINGS".
std::string StringAt(const std::vector<uint8_t>& strings, uint32_t offset)
{
	if (offset == 0 || offset >= strings.size())
		return "";
	size_t end = offset;
	while (end < strings.size() && strings[end] != 0)
		++end;
	return std::string(reinterpret_cast<const char*>(strings.data() + offset), end - offset);
}

std::string ArchivePath(const std::string& s)
{
	return s.empty() ? s : SystemString(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
} // namespace
//------------------------------------------------------------------------
ChmSystemInfo ParseChmSystem(const std::vector<uint8_t>& sys)
{
	// DWORD version, then records { WORD code, WORD length, data }.
	ChmSystemInfo info;
	size_t pos = 4;
	while (pos + 4 <= sys.size())
	{
		const unsigned code = sys[pos] | (sys[pos + 1] << 8);
		const size_t len = sys[pos + 2] | (sys[pos + 3] << 8);
		pos += 4;
		if (pos + len > sys.size())
			break;
		const uint8_t* data = sys.data() + pos;
		if (code == 0 && info.toc.empty())
			info.toc = SystemString(data, len);
		else if (code == 2 && info.start.empty())
			info.start = SystemString(data, len);
		else if (code == 4 && len >= 4)
			info.lcid = data[0] | (data[1] << 8) | (data[2] << 16) | (static_cast<unsigned>(data[3]) << 24);
		else if (code == 5 && info.defaultWindow.empty())
		{
			std::string w(reinterpret_cast<const char*>(data), len);
			if (const auto nul = w.find('\0'); nul != std::string::npos)
				w.resize(nul);
			info.defaultWindow = w;
		}
		pos += len;
	}
	return info;
}
//------------------------------------------------------------------------
ChmWindowInfo ParseChmWindows(const std::vector<uint8_t>& windows, const std::vector<uint8_t>& strings,
                              const std::string& defaultWindow)
{
	ChmWindowInfo info;
	const uint32_t count = Dword(windows, 0);
	const uint32_t size = Dword(windows, 4);
	if (count == 0 || size < 0x78 || size > 0x1000)
		return info;

	size_t chosen = SIZE_MAX;
	for (uint32_t i = 0; i < count; ++i)
	{
		const size_t entry = 8 + static_cast<size_t>(i) * size;
		if (entry + size > windows.size())
			break;
		if (chosen == SIZE_MAX)
			chosen = entry;
		if (!defaultWindow.empty() && LowerAscii(StringAt(strings, Dword(windows, entry + 0x08))) == LowerAscii(defaultWindow))
		{
			chosen = entry;
			break;
		}
	}
	if (chosen == SIZE_MAX)
		return info;

	info.toc = ArchivePath(StringAt(strings, Dword(windows, chosen + 0x60)));
	info.start = ArchivePath(StringAt(strings, Dword(windows, chosen + 0x68)));
	info.home = ArchivePath(StringAt(strings, Dword(windows, chosen + 0x6C)));
	info.navClosed = Dword(windows, chosen + 0x74) == 1;
	return info;
}
//------------------------------------------------------------------------
std::string ChmPageCharset(const std::vector<uint8_t>& page, const std::string& languageCharset)
{
	const size_t n = page.size();
	if ((n >= 3 && page[0] == 0xEF && page[1] == 0xBB && page[2] == 0xBF) ||
	    (n >= 2 && ((page[0] == 0xFF && page[1] == 0xFE) || (page[0] == 0xFE && page[1] == 0xFF))))
		return "";

	{
		const std::string head = LowerAscii(std::string(reinterpret_cast<const char*>(page.data()), std::min<size_t>(n, 1024)));
		for (size_t m = head.find("<meta"); m != std::string::npos; m = head.find("<meta", m + 5))
		{
			const size_t end = head.find('>', m);
			const size_t cs = head.find("charset", m);
			if (cs != std::string::npos && (end == std::string::npos || cs < end))
				return "";
		}
	}

	bool nonAscii = false;
	bool utf8 = true;
	for (size_t i = 0; i < n;)
	{
		const uint8_t c = page[i];
		if (c < 0x80) { ++i; continue; }
		nonAscii = true;
		size_t len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
		if (len == 0 || c == 0xC0 || c == 0xC1 || c > 0xF4 || i + len > n) { utf8 = false; break; }
		for (size_t k = 1; k < len; ++k)
			if ((page[i + k] & 0xC0) != 0x80) { utf8 = false; break; }
		if (!utf8)
			break;
		i += len;
	}
	return !nonAscii ? "" : utf8 ? "utf-8" : languageCharset;
}
//------------------------------------------------------------------------
bool ParseChmUri(const std::wstring& uri, std::wstring& escapedPath, std::wstring& query)
{
	escapedPath.clear();
	query.clear();
	if (!IsChmUri(uri))
		return false;
	std::wstring rest = uri.substr(std::wstring_view(L"evh://chm.example/").size());
	if (const auto h = rest.find(L'#'); h != std::wstring::npos)
		rest.resize(h);
	if (const auto q = rest.find(L'?'); q != std::wstring::npos)
	{
		query = rest.substr(q + 1);
		rest.resize(q);
	}
	escapedPath = rest;
	return true;
}
//------------------------------------------------------------------------
std::string ChmCodePageLabel(unsigned lcid)
{
#ifdef _WIN32
	if (lcid == 0)
		return "";
	DWORD cp = 0;
	if (!GetLocaleInfoW(static_cast<LCID>(lcid), LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
	                    reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(wchar_t)))
		return "";
	switch (cp)
	{
		case 874: return "windows-874";
		case 932: return "shift_jis";
		case 936: return "gbk";
		case 949: return "euc-kr";
		case 950: return "big5";
		case 1250: case 1251: case 1252: case 1253: case 1254:
		case 1255: case 1256: case 1257: case 1258:
			return "windows-" + std::to_string(cp);
		default: return "";
	}
#else
	(void)lcid;
	return "";
#endif
}
//------------------------------------------------------------------------
std::wstring ChmJsString(std::wstring_view s)
{
	static const wchar_t hex[] = L"0123456789abcdef";
	std::wstring out = L"\"";
	for (wchar_t c : s)
	{
		if (c == L'\\' || c == L'"' || c == L'<' || c < 0x20 || c == 0x2028 || c == 0x2029)
		{
			out += L"\\u";
			for (int shift = 12; shift >= 0; shift -= 4)
				out += hex[(c >> shift) & 0xF];
		}
		else
			out += c;
	}
	return out + L"\"";
}
//------------------------------------------------------------------------

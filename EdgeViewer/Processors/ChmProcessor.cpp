#include "ChmProcessor.h"
#include "../ChmArchive.h"
#include "../Globals.h"

#include <atomic>
#include <string>

//------------------------------------------------------------------------
namespace
{
ChmProcessor chm;

// Archive path ("/dir/a b.htm#x") -> relative URL ("dir/a%20b.htm#x").
std::wstring EntryUrl(const std::string& entry)
{
	std::string path = entry;
	std::string fragment;
	if (const auto hash = path.find('#'); hash != std::string::npos)
	{
		fragment = path.substr(hash);
		path.resize(hash);
	}
	while (!path.empty() && path[0] == '/')
		path.erase(0, 1);

	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : path)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
			out += static_cast<char>(c);
		else
		{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	// The fragment goes into a JS string: keep only safe characters.
	for (unsigned char c : fragment)
		if (c > 0x20 && c < 0x7F && c != '"' && c != '\'' && c != '\\' && c != '<' && c != '>' && c != '&')
			out += static_cast<char>(c);
	return to_utf16(out);
}

std::wstring HtmlEscape(const std::wstring& s)
{
	std::wstring out;
	for (wchar_t c : s)
	{
		switch (c)
		{
			case L'&': out += L"&amp;"; break;
			case L'<': out += L"&lt;"; break;
			case L'>': out += L"&gt;"; break;
			case L'"': out += L"&quot;"; break;
			default: out += c;
		}
	}
	return out;
}

// Plain replacement: no regex, '$' in the value is not special.
void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to)
{
	for (size_t pos = s.find(from); pos != std::wstring::npos; pos = s.find(from, pos + to.size()))
		s.replace(pos, from.size(), to);
}

std::wstring ErrorPage(const std::wstring& text)
{
	return L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
	       L"<body style=\"font:14px system-ui,sans-serif;padding:16px\"><p>" + HtmlEscape(text) + L"</p></body></html>";
}
} // namespace
//------------------------------------------------------------------------
bool ChmProcessor::InitPath(const std::filesystem::path& path)
{
	mPath = GetPhysicalPath(path);
	if (!std::filesystem::is_regular_file(mPath))
		return false;
	return isType(path.extension(), "CHM");
}
//------------------------------------------------------------------------
void ChmProcessor::OpenIn(IWebView& webView) const
{
	mapDomains(webView, mPath.root_path());

	auto archive = ChmArchive::Open(mPath);
	if (!archive)
	{
		webView.NavigateToString(ErrorPage(L"EdgeViewer: cannot open this CHM file."));
		return;
	}

	std::wstring loader = to_utf16(ReadFile(assetsPath() / L"chm" / L"loader.html"));

	ReplaceAll(loader, L"__CHM_TITLE__", HtmlEscape(mPath.filename().wstring()));
	ReplaceAll(loader, L"__CHM_START__", EntryUrl(archive->StartTopic()));
	ReplaceAll(loader, L"__CHM_HOME__", EntryUrl(archive->HomeTopic()));
	ReplaceAll(loader, L"__CHM_TOC__", EntryUrl(archive->TocPath()));
	ReplaceAll(loader, L"__CHM_DARK__", gs_IsDarkMode ? L"1" : L"0");
	ReplaceAll(loader, L"__CHM_NAVCLOSED__", archive->NavClosed() ? L"1" : L"0");
	ReplaceAll(loader, L"__CHM_CODEPAGE__", to_utf16(archive->CodePageLabel()));
	// [CHM] CSS / CSSDark is inlined (the loader copies it into every page).
	// Replaced last: the CSS text is not scanned for placeholders.
	std::wstring css;
	if (const auto cssFile = GlobalSettings()["CHM"][gs_IsDarkMode ? "CSSDark" : "CSS"]; !cssFile.empty())
		css = to_utf16(ReadFile(assetsPath() / L"chm" / to_utf16(cssFile)));
	ReplaceAll(css, L"</", L"<\\/"); // cannot close the <style> element
	ReplaceAll(loader, L"__CHM_CSS__", css);

	// A fresh id per open: a stale page cannot load the new loader.
	static std::atomic<unsigned> counter{ 0 };
	const std::wstring id = std::to_wstring(++counter);

	webView.RegisterChmArchive(id, std::move(archive), loader);
	webView.Navigate(L"evh://chm.example/?evchm=" + id);

	// Encoding menu. After Navigate, which resets these flags.
	webView.SetEncodingOverrideSupported(true);
	webView.SetEncodingOverrideHtml(false);
}
//------------------------------------------------------------------------

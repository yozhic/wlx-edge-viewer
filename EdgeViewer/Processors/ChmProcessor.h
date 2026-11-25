#pragma once

#include "ProcessorInterface.h"
#include <filesystem>

//------------------------------------------------------------------------
// CHM (Compiled HTML Help). The view serves the loader page
// (assets/chm/loader.html) at evh://chm.example/?evchm=<id> and the archive
// entries at evh://chm.example/<path>.
class ChmProcessor : public ProcessorInterface
{
public:
	bool InitPath(const std::filesystem::path& path) override;
	void OpenIn(IWebView& webView) const override;

private:
	std::filesystem::path mPath;
};
//------------------------------------------------------------------------

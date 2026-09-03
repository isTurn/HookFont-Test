#pragma once
#include <string>

#include "Str_Trim.h"

//Ria's Utility Library X
namespace Rut
{
	namespace StrX
	{
		std::locale& GetCVT_UTF8();

		// CodePage is fundamentally a UINT; using the exact type avoids size_t→UINT
		// truncation warnings on 64-bit builds.
		std::wstring StrToWStr(const std::string& msString, unsigned int uCodePage);
		std::string  WStrToStr(const std::wstring& wsString, unsigned int uCodePage);
		size_t       StrToWStr(const std::string& msString, std::wstring& wsString, unsigned int uCodePage);
		size_t       WStrToStr(const std::wstring& wsString, std::string& msString, unsigned int uCodePage);

		std::wstring StrToWStr_CVT(const std::string& msString, unsigned int uCodePage);
		std::string  WStrToStr_CVT(const std::wstring& wsString, unsigned int uCodePage);
		void         StrToWStr_CVT(const std::string& msString, std::wstring& wsString, unsigned int uCodePage);
		void         WStrToStr_CVT(const std::wstring& wsString, std::string& msString, unsigned int uCodePage);
	}
}
#include "INI.h"
#include "Str.h"
#include "File.h"

#include <sstream>
#include <vector>


namespace Rcf
{
	namespace INI
	{
		using namespace Rut::StrX;
		using namespace Rut::FileX;

		INI_File::INI_File()
		{

		}

		INI_File::INI_File(const std::wstring & wsINI)
		{
			Parse(wsINI);
		}

		// Decode a raw byte buffer to wide text. cp == 0xFFFF means raw UTF-16LE
		// (already native endianness on Windows); everything else goes through
		// MultiByteToWideChar.
		static std::wstring DecodeBuffer(const BYTE* p, DWORD n, UINT cp)
		{
			if (cp == 0xFFFF)
			{
				return std::wstring(reinterpret_cast<const wchar_t*>(p), n / 2);
			}
			int cch = MultiByteToWideChar(cp, 0, reinterpret_cast<LPCSTR>(p), (int)n, NULL, 0);
			if (cch <= 0) return std::wstring();
			std::wstring wsOut(cch, L'\0');
			MultiByteToWideChar(cp, 0, reinterpret_cast<LPCSTR>(p), (int)n, &wsOut[0], cch);
			return wsOut;
		}

		// Read an INI file tolerantly: UTF-8 with or without BOM (the normal case),
		// UTF-16LE with BOM, or ANSI/GBK when the bytes are not valid UTF-8 — so a
		// config saved by Notepad as "ANSI" or "Unicode" still parses instead of
		// producing mojibake keys that silently match nothing.
		static std::wstring ReadFileSmart(const std::wstring& wsPath)
		{
			HANDLE hFile = CreateFileW(wsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile == INVALID_HANDLE_VALUE) return std::wstring();

			DWORD dwSize = GetFileSize(hFile, NULL);
			std::vector<BYTE> vec(dwSize ? dwSize : 1);
			DWORD dwRead = 0;
			BOOL bOk = ReadFile(hFile, vec.data(), dwSize, &dwRead, NULL);
			CloseHandle(hFile);
			if (!bOk || dwRead == 0) return std::wstring();

			const BYTE* p = vec.data();
			DWORD n = dwRead;

			if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) return DecodeBuffer(p + 3, n - 3, CP_UTF8);
			if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)              return DecodeBuffer(p + 2, n - 2, 0xFFFF); // UTF-16LE
			if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)              return std::wstring(); // UTF-16BE: too rare, skip

			// No BOM: strict UTF-8 check first (ASCII-only files are valid UTF-8 too),
			// then fall back to the ANSI code page (GBK on Chinese Windows).
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCSTR>(p), (int)n, NULL, 0) > 0)
				return DecodeBuffer(p, n, CP_UTF8);
			return DecodeBuffer(p, n, CP_ACP);
		}

		void INI_File::Parse(const std::wstring& wsINI)
		{
			std::wistringstream wiss(ReadFileSmart(wsINI));

			std::size_t pos = std::wstring::npos;
			std::wstring node_name;
			for (std::wstring line; std::getline(wiss, line);)
			{
				// Manual byte reading bypasses the text-stream CRLF->LF translation, so a
				// CRLF file leaves a trailing '\r' on every line. Strip it so blank lines
				// (and every key/value line) parse like they did through wifstream.
				if (!line.empty() && line.back() == L'\r') line.pop_back();
				if (line.empty()) { continue; }

				switch (line[0])
				{
				case L'#':case L';':case L'/': // Comment Char
					break;

				case L'[': // [Node]
				{
					pos = line.find_first_of(L']');
					if (pos == std::wstring::npos) { throw std::runtime_error("INI_File:Parse: Get Node Error!"); }
					node_name = Trim(line.substr(1, pos - 1));
				}
				break;

				default:// Key = Value
				{
					pos = line.find_first_of(L'=');
					if ((pos == std::wstring::npos) || (pos == 0)) { throw std::runtime_error("INI_File::Parse: Get Key Error!"); }
					Name key = Trim(line.substr(0, pos));
					m_mpNodes[node_name][key] = Trim(line.substr(pos + 1));
					// remember definition order (dedup by key)
					auto& order = m_mpKeyOrder[node_name];
					if (std::find(order.begin(), order.end(), key) == order.end())
						order.push_back(key);
				}
				break;
				}
			}
		}

		void INI_File::Save(const std::wstring& wsFile)
		{
			std::wofstream wofs_ini = CreateFileUTF8Stream(wsFile);
			wofs_ini << Dump();
		}

		std::wstring INI_File::Dump()
		{
			std::wstringstream ss;
			for (auto& node : m_mpNodes)
			{
				ss << L"[" << node.first << L"]" << L'\n';
				for (auto& key : node.second) { ss << key.first << L"=" << std::wstring(key.second) << L'\n'; }
				ss << L'\n';
			}
			return ss.str();
		}


		NodesMap::iterator INI_File::At(const std::wstring& wsNode)
		{
			return m_mpNodes.find(wsNode);
		}

		NodesMap::iterator INI_File::End()
		{
			return m_mpNodes.end();
		}

		KeysMap& INI_File::Get(const std::wstring& wsNode)
		{
			const auto& ite_node = At(wsNode);
			if (ite_node == End()) { throw std::runtime_error("INI_File::Get: INI File No Find Node"); }
			return ite_node->second;
		}

		Value& INI_File::Get(const std::wstring& wsNode, const std::wstring& wsName)
		{
			auto& keys = Get(wsNode);
			const auto& ite_keys = keys.find(wsName);
			if (ite_keys == keys.end()) { throw std::runtime_error("INI_File::Get: INI File No Find Key"); }
			return ite_keys->second;
		}

		KeysMap& INI_File::operator[] (const std::wstring& wsNode)
		{
			return Get(wsNode);
		}

		void INI_File::Add(const std::wstring& wsNode, const std::wstring& wsName, const Value& vValue)
		{
			m_mpNodes[wsNode][wsName] = vValue;
		}

		bool INI_File::Has(const std::wstring& wsNode)
		{
			return At(wsNode) != End() ? true : false;
		}

		bool INI_File::Has(const std::wstring& wsNode, const std::wstring& wsName)
		{
			auto ite_node = At(wsNode);
			if (ite_node != End())
			{
				auto& keys = ite_node->second;
				auto ite_keys = keys.find(wsName);
				return ite_keys != keys.end() ? true : false;
			}
			return false;
		}

		bool INI_File::TryGet(const std::wstring& wsNode, const std::wstring& wsName, Value& vOut)
		{
			auto ite_node = At(wsNode);
			if (ite_node == End()) { return false; }

			auto ite_keys = ite_node->second.find(wsName);
			if (ite_keys == ite_node->second.end()) { return false; }

			vOut = ite_keys->second;
			return true;
		}

		std::vector<std::pair<Name, Value>> INI_File::GetOrdered(const std::wstring& wsNode)
		{
			std::vector<std::pair<Name, Value>> vOut;

			auto ite_node = At(wsNode);
			if (ite_node == End()) { return vOut; }

			auto ite_order = m_mpKeyOrder.find(wsNode);
			if (ite_order == m_mpKeyOrder.end())
			{
				// Fallback: unordered iteration (order not recorded).
				for (auto& kv : ite_node->second) vOut.emplace_back(kv.first, kv.second);
				return vOut;
			}

			vOut.reserve(ite_order->second.size());
			for (const Name& key : ite_order->second)
			{
				auto ite_keys = ite_node->second.find(key);
				if (ite_keys != ite_node->second.end())
					vOut.emplace_back(ite_keys->first, ite_keys->second);
			}
			return vOut;
		}
	}
}

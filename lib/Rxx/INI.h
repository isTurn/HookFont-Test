#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>

#include "INI_Value.h"

//Ria's Configuration File Library X 
namespace Rcf
{
	// Reference yazi-ini
	namespace INI
	{
		typedef std::wstring							Name;
		typedef std::wstring							NodeName;
		typedef std::unordered_map<Name, Value>			KeysMap;
		typedef std::unordered_map<NodeName, KeysMap>	NodesMap;

		class INI_File
		{
		private:
			NodesMap m_mpNodes;
			// Definition order of keys inside each node (unordered_map does not keep it).
			std::unordered_map<NodeName, std::vector<Name>> m_mpKeyOrder;

		private:
			NodesMap::iterator At(const std::wstring& wsNode);
			NodesMap::iterator End();
			void Parse(const std::wstring& wsINI);

		public:
			INI_File();
			INI_File(const std::wstring& wsINI);

			std::wstring Dump();
			void Save(const std::wstring& wsFile);
			friend std::wostream& operator << (std::wostream& woStream, INI_File& iniFile) { woStream << iniFile.Dump(); return woStream; }

			KeysMap& operator[] (const std::wstring& wsNode);
			KeysMap& Get(const std::wstring& wsNode);
			Value& Get(const std::wstring& wsNode, const std::wstring& wsName);
			void Add(const std::wstring& wsNode, const std::wstring& wsName, const Value& vValue);
			bool Has(const std::wstring& wsNode);
			bool Has(const std::wstring& wsNode, const std::wstring& wsName);

			// Non-throwing read: fills vOut when the key exists, returns false otherwise.
			bool TryGet(const std::wstring& wsNode, const std::wstring& wsName, Value& vOut);

			// Ordered (definition-order) read of a whole node; empty when the node is missing.
			std::vector<std::pair<Name, Value>> GetOrdered(const std::wstring& wsNode);

		};
	}
}

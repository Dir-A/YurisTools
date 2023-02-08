#include "YSTB.h"
#include "..\TDA\CVTString.h"
#include "..\TDA\FileX.h"

#include <Windows.h>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace YurisStaticLibrary
{
	using namespace YSTB_Struct;
	using namespace TDA;

	bool YSTB::TextInset_V2(std::wstring wsFileName, unsigned int uiCodePage)
	{
		//Backup File
		CopyFileW(wsFileName.c_str(), (wsFileName + L".new").c_str(), FALSE);

		//Open Text File
		std::wifstream iTextStream(wsFileName + L".txt");
		iTextStream.imbue(CVTString::GetCVT_UTF8());
		if (!iTextStream.is_open())
		{
			std::wcout << "Open Text File Failed!" << L'\n';
			return false;
		}

		//Open YSTB File
		std::fstream ioYSTBStream(wsFileName + L".new", std::ios::in | std::ios::out | std::ios::binary);
		if (!ioYSTBStream.is_open())
		{
			std::wcout << "Open YSTB File Failed!" << L'\n';
			return false;
		}

		//Init Header
		YSTBHeader_V2 header = { 0 };
		ioYSTBStream.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (*(unsigned int*)header.aSignature != 0x42545359)
		{
			std::wcout << "Mismatched File Header!" << L'\n';
			return false;
		}

		//Init Buffer
		char* pCodeSeg = new char[header.uiCodeSegSize];
		ioYSTBStream.read(pCodeSeg, header.uiCodeSegSize);

		//Append Text
		std::string mText;
		unsigned int szText = 0;
		unsigned int offset = 0;
		for (std::wstring wLine; std::getline(iTextStream, wLine);)
		{
			if (wLine.find(L"CodePos:") != 0) continue;

			swscanf_s(wLine.c_str(), L"CodePos:%08x", &offset);

			for (; std::getline(iTextStream, wLine);)
			{
				if (wLine.find(L"Tra:") != 0) continue;

				//Processing Text
				wLine = wLine.substr(4);
				CVTString::WStrToStr(wLine, mText, uiCodePage);
				szText = mText.size();

				//Modify Code (len,off) and File Header
				*(unsigned int*)(pCodeSeg + offset + 0xA) = szText;
				*(unsigned int*)(pCodeSeg + offset + 0xE) = header.uiResSegSize;
				header.uiResSegSize += szText;

				//Append Text
				ioYSTBStream.seekp(0, std::ios::end);
				ioYSTBStream.write(mText.c_str(), szText);

				break;
			}
		}

		//Write Back Header
		ioYSTBStream.seekp(0);
		ioYSTBStream.write(reinterpret_cast<char*>(&header), sizeof(header));

		//Write Back Code Segment
		ioYSTBStream.seekp(sizeof(YSTBHeader_V2));
		ioYSTBStream.write(pCodeSeg, header.uiCodeSegSize);

		//Clean
		ioYSTBStream.seekp(0);
		ioYSTBStream.flush();
		delete[] pCodeSeg;

		return true;
	}

	void ParameterAnalysis(std::wofstream& woText, std::string& mText, std::wstring& wText, Instruction_V2* pIns, char* pCodeSeg, char* pResSeg, size_t iteCodeSize, unsigned int uiCodePage)
	{
		size_t szBlock = pIns->ucArgs * 0xC + 6;
		ResEntry_V2* pEntry = (ResEntry_V2*)(pCodeSeg + iteCodeSize + 0x6);

		woText
			<< L"Off:0x"
			<< std::setw(0x8) << std::setfill(L'0') << std::hex
			<< sizeof(YSTBHeader_V2) + iteCodeSize << L" ";

		for (size_t iteArg = 0; iteArg < (szBlock - 6) / 12; iteArg++)
		{
			if (*(pResSeg + pEntry->uiResRVA) == 0x4D && *(pResSeg + pEntry->uiResRVA + 3) != 0x27)
			{
				mText.resize(pEntry->uiResSize - 5);
				memcpy(const_cast<char*>(mText.data()), pResSeg + pEntry->uiResRVA + 4, pEntry->uiResSize - 5);
				CVTString::StrToWStr(mText, wText, uiCodePage);
				woText << wText << L', ';
			}
			else
			{
				long long buffer = 0;
				if ((pEntry->uiResSize - 3) > 4)
				{
					memcpy(&buffer, pResSeg + pEntry->uiResRVA + 3, pEntry->uiResSize - 3);
					woText << buffer << L',';
				}
				else
				{
					memcpy(&buffer, pResSeg + pEntry->uiResRVA + 3, pEntry->uiResSize - 3);
					woText << buffer << L', ';
				}

			}

			pEntry += 1; //Move To Next Entry
		}

		woText << std::endl;
	}

	bool YSTB::TextDump_V2(std::wstring wsFileName, unsigned int uiCodePage, bool isAudioFileName)
	{
		//Create Text File
		std::wofstream woText(wsFileName + L".txt");
		woText.imbue(CVTString::GetCVT_UTF8());
		if (!woText.is_open())
		{
			std::wcout << "Create Text File Failed!" << L'\n';
			return false;
		}

		//Open YSTB File
		std::ifstream ioYSTBStream(wsFileName, std::ios::binary);
		if (!ioYSTBStream.is_open())
		{
			std::wcout << "Create YSTB File Failed!" << L'\n';
			return false;
		}

		//Init Header
		YSTBHeader_V2 header = { 0 };
		ioYSTBStream.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (*(unsigned int*)header.aSignature != 0x42545359)
		{
			std::wcout << "Mismatched File Header!" << L'\n';
			return false;
		}

		//Init Seg Buffer
		char* pCodeSeg = new char[header.uiCodeSegSize];
		ioYSTBStream.read(pCodeSeg, header.uiCodeSegSize);
		char* pResSeg = new char[header.uiResSegSize];
		ioYSTBStream.read(pResSeg, header.uiResSegSize);

		//Analyzing VM
		std::string mText;
		std::wstring wText;
		unsigned int count = 0;
		for (size_t iteCodeSize = 0; iteCodeSize < header.uiCodeSegSize;)
		{
			Instruction_V2* pIns = (Instruction_V2*)(pCodeSeg + iteCodeSize);

			switch (pIns->ucOP)
			{
			case 0x38:
			{
				iteCodeSize += 0xA;
				continue;
			}
			break;

			//Parameter analysis
			case 0x19:
			{
				if (isAudioFileName)
				{
					size_t szBlock = pIns->ucArgs * 0xC + 6;
					ResEntry_V2* pEntry = (ResEntry_V2*)(pCodeSeg + iteCodeSize + 0x6);

					if (*(pResSeg + pEntry->uiResRVA) == 0x4D && *(pResSeg + pEntry->uiResRVA + 3) != 0x27)
					{
						mText.resize(pEntry->uiResSize - 5);
						memcpy(const_cast<char*>(mText.data()), pResSeg + pEntry->uiResRVA + 4, pEntry->uiResSize - 5);

						if (mText.find("es.VoiceSetTask") != 0)
						{
							break;
						}

						pEntry += 1;

						mText.resize(pEntry->uiResSize - 5);
						memcpy(const_cast<char*>(mText.data()), pResSeg + pEntry->uiResRVA + 4, pEntry->uiResSize - 5);
						CVTString::StrToWStr(mText, wText, uiCodePage);
					}
				}
				//ParameterAnalysis(woText, mText, wText, pIns, pCodeSeg, pResSeg, iteCodeSize, uiCodePage);
			}
			break;

			//Game Text
			case 0x54:
			{
				if (isAudioFileName)
				{
					woText << L"Audio:" << wText << L'\n';
				}

				count++;

				ResEntry_V2* pEntry = (ResEntry_V2*)(pCodeSeg + iteCodeSize + 0x6);
				mText.resize(pEntry->uiResSize);
				memcpy(const_cast<char*>(mText.data()), pResSeg + pEntry->uiResRVA, pEntry->uiResSize);
#ifdef _DEBUG
				woText
					<< L"StrOffset:"
					<< std::setw(0x8) << std::setfill(L'0') << std::hex
					<< (int*)(header.uiCodeSegSize + pEntry->uiResRVA + sizeof(header)) << L'\n';

				woText
					<< L"CodeOffset:"
					<< std::setw(0x8) << std::setfill(L'0') << std::hex
					<< iteCodeSize + sizeof(header) << L'\n';
#endif
				woText 
					<< L"CodePos:" 
					<< std::setw(0x8) << std::setfill(L'0') << std::hex
					<< iteCodeSize << L'\n';

				woText 
					<< L"Count  :" << std::setw(0x8) << std::setfill(L'0') << std::dec 
					<< count << L'\n';

				CVTString::StrToWStr(mText, wText, uiCodePage);

				woText
					<< L"Raw:" << wText << L"\n"
					<< L"Tra:" << wText << L"\n\n";

				wText.clear();
			}
			break;

			}

			//Size Of Code Block
			iteCodeSize += pIns->ucArgs * 0xC + 6;
		}

		//Clean
		woText.flush();
		woText.close();
		delete[] pCodeSeg;
		delete[] pResSeg;

		if (count == 0)
		{
			DeleteFileW((wsFileName + L".txt").c_str());
		}

		return true;
	}

	bool YSTB::XorScript(std::string strYSTB, unsigned char* aXorKey)
	{
		char* pSeg = nullptr;
		char* pYSTB = nullptr;
		unsigned int magic = 0;
		unsigned int version = 0;
		std::streamsize size = 0;


		std::ifstream iYSTB(strYSTB, std::ios::binary);
		if (!iYSTB.is_open()) return false;

		//Check is YSTB file
		iYSTB.read(reinterpret_cast<char*>(&magic), 4);
		iYSTB.read(reinterpret_cast<char*>(&version), 4);
		iYSTB.seekg(0, std::ios::beg);
		if (magic != 0x42545359) { iYSTB.close(); return false; }

		//Read Buffer
		size = FileX::GetFileSize(iYSTB);
		pYSTB = new char[static_cast<size_t>(size)];
		iYSTB.read(pYSTB, size);

		//Skip File Header
		pSeg = pYSTB + 0x20;

		//Dec
		if (version > 200 && version < 300)
		{
			YSTBHeader_V2* pHeader = reinterpret_cast<YSTBHeader_V2*>(pYSTB);
			for (size_t iteData = 0; iteData < static_cast<size_t>(pHeader->uiCodeSegSize); iteData++)
			{
				pSeg[iteData] ^= aXorKey[iteData & 3];
			}

			pSeg += pHeader->uiCodeSegSize;

			for (size_t iteData = 0; iteData < static_cast<size_t>(pHeader->uiResSegSize); iteData++)
			{
				pSeg[iteData] ^= aXorKey[iteData & 3];
			}
		}
		else
		{
			size_t szSegment = 0;
			for (size_t iteSegments = 0; iteSegments < 4; iteSegments++)
			{
				szSegment = *(reinterpret_cast<unsigned int*>(pYSTB) + 3 + iteSegments);
				for (size_t iteData = 0; iteData < szSegment; iteData++)
				{
					pSeg[iteData] ^= aXorKey[iteData & 3];
				}
				pSeg += szSegment;
			}
		}
		pSeg = nullptr;

		//Save Buffer To New File
		std::ofstream oYSTB(strYSTB + ".dec", std::ios::binary);
		if (oYSTB.is_open())
		{
			oYSTB.write(pYSTB, size);
			oYSTB.flush();
			oYSTB.close();
		}

		delete[] pYSTB;
		pYSTB = nullptr;

		iYSTB.close();
		return true;
	}

	void YSTB::GuessXorKey(std::string strYSTB, unsigned char* aXorKey)
	{
		unsigned int key = 0;
		unsigned int version = 0;

		std::ifstream iYSTB(strYSTB, std::ios::binary);
		if (!iYSTB.is_open()) return;

		//Read Version
		iYSTB.seekg(4);
		iYSTB.read(reinterpret_cast<char*>(&version), 4);
		iYSTB.seekg(0);

		//Check Version
		if (version > 200 && version < 300)
		{
			YSTBHeader_V2 header = { 0 };
			iYSTB.read(reinterpret_cast<char*>(&header), sizeof(YSTBHeader_V2));
			if ((header.uiCodeSegSize + header.uiResSegSize) < 0x10)
			{
				*reinterpret_cast<unsigned int*>(aXorKey) = 0;
			}
			else
			{
				iYSTB.seekg(0x2C);
				iYSTB.read(reinterpret_cast<char*>(&key), 4);
			}
		}
		else
		{
			//Init Header
			YSTBHeader_V5 header = { 0 };
			iYSTB.read(reinterpret_cast<char*>(&header), sizeof(YSTBHeader_V5));

			if (header.uiAttributeValuesSize == 0)
			{
				*reinterpret_cast<unsigned int*>(aXorKey) = 0;
			}
			else
			{
				//Read The First AttributeDescriptor iOffset == 0
				iYSTB.seekg(static_cast<std::streampos>(header.uiInstructionsSize + sizeof(YSTBHeader_V5) + 0x8));
				iYSTB.read(reinterpret_cast<char*>(&key), 4);
			}
		}

		*reinterpret_cast<unsigned int*>(aXorKey) = key;

		iYSTB.close();
	}

}


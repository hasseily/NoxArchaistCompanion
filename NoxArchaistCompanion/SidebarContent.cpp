#include "pch.h"
#include "SidebarContent.h"
#include "Emulator/Memory.h"
#include <shobjidl_core.h> 
#include <DirectXPackedVector.h>
#include <DirectXMath.h>
#include <Sidebar.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace DirectX::PackedVector;
using namespace std;
namespace fs = std::filesystem;

static string SIDEBAR_FORMAT_PLACEHOLDER("{}");

static UINT8* pmem;
static int memsize;

SidebarContent::SidebarContent()
{
    Initialize();
}

void SidebarContent::Initialize()
{
    // Get memory start address
    memsize = 0x20000;  // 128k of RAM
}

bool SidebarContent::setActiveProfile(SidebarManager* sbM, std::string* name)
{
    if (name->empty())
    {
        // this happens if we can't read the shm
        // TODO: load a default profile that says that AW isn't running?
        // Or put other default info?
        return false;
    }
    /*
    if ((!force) && (m_activeProfile["meta"]["name"] == *name))
    {
        // no change
        return true;
    }
    */
	nlohmann::json j;
    try {
        j = m_allProfiles.at(*name);
    }
    catch (std::out_of_range const& exc) {
        m_activeProfile["meta"]["name"] = *name;
        char buf[sizeof(exc.what()) + 500];
        snprintf(buf, 500, "Profile %s doesn't exist: %s\n", name->c_str(), exc.what());
        OutputDebugStringA(buf);
        return false;
    }

    m_activeProfile = j;
    //OutputDebugStringA(j.dump().c_str());
    //OutputDebugStringA(j["sidebars"].dump().c_str());

    sbM->DeleteAllSidebars();
    string title;
    auto numSidebars = (UINT8)m_activeProfile["sidebars"].size();
    for (UINT8 i = 0; i < numSidebars; i++)
    {
        nlohmann::json sj = m_activeProfile["sidebars"][i];
        auto numBlocks = static_cast<UINT8>(sj["blocks"].size());
        if (numBlocks == 0)
        {
            continue;
        }

        SidebarTypes st = SidebarTypes::Right;  // default
        UINT16 sSize = 0;
        if (sj["type"] == "Bottom")
        {
            st = SidebarTypes::Bottom;
        }
        switch (st)
        {
        case SidebarTypes::Right:
            if (sj.contains("width"))
                sSize = sj["width"];
            break;
        case SidebarTypes::Bottom:
            if (sj.contains("height"))
                sSize = sj["height"];
            break;
        default:
            break;
        }
        UINT8 sbId;
        sbM->CreateSidebar(st, numBlocks, sSize, &sbId);
        for (UINT8 k = 0; k < numBlocks; k++)
        {
            nlohmann::json bj = sj["blocks"][k];
            // OutputDebugStringA((bj.dump() + string("\n")).c_str());
            BlockStruct bS;

            if (bj["type"] == "Header")
            {
                bS.color = Colors::CadetBlue;
                bS.type = BlockType::Header;
                bS.fontId = FontDescriptors::A2FontBold;
            }
            else if (bj["type"] == "Empty")
            {
                bS.color = Colors::Black;
                bS.type = BlockType::Empty;
                bS.fontId = FontDescriptors::A2FontRegular;
            }
            else // default to "Content"
            {
                bS.color = Colors::GhostWhite;
                bS.type = BlockType::Content;
                bS.fontId = FontDescriptors::A2FontRegular;
            }
            // overrides
            if (bj.contains("color"))
            {
                bS.color = XMVectorSet(bj["color"][0], bj["color"][1], bj["color"][2], bj["color"][3]);
            }
            bS.text = "";
            sbM->sidebars[sbId].SetBlock(bS, k);
        }
    }
    return true;
}

void SidebarContent::LoadProfileUsingDialog(SidebarManager* sbM)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr))
    {
        IFileOpenDialog* pFileOpen;

        // Create the FileOpenDialog object.
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
            IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

        if (SUCCEEDED(hr))
        {
			COMDLG_FILTERSPEC rgSpec[] =
			{
				{ L"JSON Profiles", L"*.json" },
                { L"All Files", L"*.*" },
			};
            pFileOpen->SetFileTypes(2, rgSpec);
            // Show the Open dialog box.
            hr = pFileOpen->Show(nullptr);
            // Get the file name from the dialog box.
            if (SUCCEEDED(hr))
            {
                IShellItem* pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr))
                {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    // Display the file name to the user.
                    if (SUCCEEDED(hr))
                    {
                        fs::directory_entry dir = fs::directory_entry(pszFilePath);
                        std::string profileName = OpenProfile(dir);
                        setActiveProfile(sbM, &profileName);
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
        CoUninitialize();
    }
}

void SidebarContent::LoadProfilesFromDisk()
{
    if(!m_allProfiles.empty())
        m_allProfiles.clear();
    fs::path currentDir = fs::current_path();
    currentDir += "\\Profiles";
    
    for (const auto& entry : fs::directory_iterator(currentDir))
    {
        OpenProfile(entry);
    }
}

std::string SidebarContent::OpenProfile(std::filesystem::directory_entry entry)
{
    Initialize();   // Might have lost the shm
//    if (entry.is_regular_file() && (entry.path().extension().compare("json")))
    if (entry.is_regular_file())
    {
        nlohmann::json profile = ParseProfile(entry.path());
        if (profile != nullptr)
        {
            string name = profile["meta"]["name"];
            if (!name.empty())
            {
                m_allProfiles[name] = profile;
                return name;
            }
        }
    }
    return "";
}

void SidebarContent::ClearActiveProfile(SidebarManager* sbM)
{
    m_activeProfile.clear();
    sbM->DeleteAllSidebars();
}

nlohmann::json SidebarContent::ParseProfile(fs::path filepath)
{
    try
    {
        std::ifstream i(filepath);
        nlohmann::json j;
        i >> j;
        return j;
    }
    catch (nlohmann::detail::parse_error err) {
        char buf[sizeof(err.what()) + 500];
        snprintf(buf, 500, "Error parsing profile: %s\n", err.what());
        OutputDebugStringA(buf);
        return nullptr;
    }
}

// Turn a variable into a string
// using the following json format:
/*
    {
        "memstart": "0x1165A",
        "length" : 16,
        "type" : "ascii_high"
    }
*/

std::string SidebarContent::SerializeVariable(nlohmann::json* pvar)
{
    nlohmann::json j = *pvar;
    // initialize variables
    // OutputDebugStringA((j.dump()+string("\n")).c_str());
    string s;
    if (j.count("length") != 1)
        return s;
    int length = j["length"];
    if (j.count("memstart") != 1)
        return s;
	if (j.count("type") != 1)
		return s;
    int memoffset = std::stoul(j["memstart"].get<std::string>(), nullptr, 0);
    if ((memoffset + length) >= memsize)    // overflow
        return s;
    if (length == 0)
        return s;
    // since the memory is contiguous, no need to check if the info is in main or aux mem
    // and since we're only reading, we don't care if the mem is dirty
    pmem = MemGetRealMainPtr(0);
	if (pmem == nullptr)
		return s;

    // now we have the memory offset and length, and we need to parse
    if (j["type"] == "ascii")
    {
        for (size_t i = 0; i < length; i++)
        {
            if (*(pmem + memoffset + i) == '\0')
                return s;
            s.append(1, (*(pmem + memoffset + i)));
        }
    }
    else if (j["type"] == "ascii_high")
    {
        // ASCII-high is basically ASCII shifted by 0x80
        for (size_t i = 0; i < length; i++)
        {
            if (*(pmem + memoffset + i) == '\0')
                return s;
            s.append(1, (*(pmem + memoffset + i) - 0x80));
        }
    }
    else if (j["type"] == "int_bigendian")
    {
        int x = 0;
        for (int i = 0; i < length; i++)
        {
            x += (*(pmem + memoffset + i)) * (int)pow(0x100, i);
        }
        s = to_string(x);
    }
    else if (j["type"] == "int_littleendian")
    {
        int x = 0;
        for (int i = 0; i < length; i++)
        {
            x += (*(pmem + memoffset + i)) * (int)pow(0x100, (length - i - 1));
        }
        s = to_string(x);
    }
    else if (j["type"] == "int_bigendian_literal")
    {
        s = "";
        char cbuf[3];
        for (int i = 0; i < length; i++)
        {
            snprintf(cbuf, 3, "%.2x", *(pmem + memoffset + i));
            s.insert(0, string(cbuf));
        }
    }
    else if (j["type"] == "int_littleendian_literal")
    {
        // int literal is like what is used in the Ultima games.
        // Garriott stored ints as literals inside memory, so for example
        // a hex 0x4523 is in fact the number 4523
        s = "";
        char cbuf[3];
        for (int i = 0; i < length; i++)
        {
            snprintf(cbuf, 3, "%.2x", *(pmem + memoffset + i));
            s.append(string(cbuf));
        }
    }
    else if (j["type"] == "lookup")
    {
        try
        {
            int x = *(pmem + memoffset);
            char buf[5000];
            snprintf(buf, 5000, "%s/0x%02x", j["lookup"].get<std::string>().c_str(), x);
            nlohmann::json::json_pointer jp(buf);
            return m_activeProfile.value(jp, "-");  // Default value when unknown lookup is "-"
        }
        catch (exception e)
        {
            std::string es = j.dump().substr(0, 4500);
            char buf[sizeof(e.what()) + 5000];
            sprintf_s(buf, "Error parsing lookup: %s\n%s\n", es.c_str(), e.what());
            OutputDebugStringA(buf);
        }
    }
    return s;
}

// This method formats the whole text block using the format string and vars
// Kind of like sprintf using the following json format:
/*
{
    "type": "Content",
    "template" : "Left: {} - Right: {}",
    "vars" : [
                {
                    "memstart": "0x1165A",
                    "length" : 4,
                    "type" : "ascii_high"
                },
                {
                    "memstart": "0x1165E",
                    "length" : 4,
                    "type" : "ascii_high"
                }
            ]
}
*/

std::string SidebarContent::FormatBlockText(nlohmann::json* pdata)
{
// serialize the SHM variables into strings
// and directly put them in the format string
    nlohmann::json data = *pdata;
    string txt;
    if (data.contains("template"))
    {
        txt = data["template"].get<string>();
    }
    array<string, SIDEBAR_MAX_VARS_IN_BLOCK> sVars;
    for (size_t i = 0; i < data["vars"].size(); i++)
    {
        sVars[i] = SerializeVariable(&(data["vars"][i]));
        txt.replace(txt.find(SIDEBAR_FORMAT_PLACEHOLDER), SIDEBAR_FORMAT_PLACEHOLDER.length(), sVars[i]);
    }
    return txt;
}

void SidebarContent::UpdateAllSidebarText(SidebarManager* sbM)
{
    UINT8 isb = 0;  // sidebar index
    for (auto sB : m_activeProfile["sidebars"])
    {
        // OutputDebugStringA((sB.dump() + string("\n")).c_str());
        UINT8 ib = 0;   // block index
        for (auto& bl : sB["blocks"])
        {
            // OutputDebugStringA((bl.dump() + string("\n")).c_str());
            if (!UpdateBlock(sbM, isb, ib, &bl))
            {
                std::cout << "Error updating block: " << ib << endl;
            }
            ib++;
        }
        isb++;
    }
}

// Update and send for display a block of text
// using the following json format:
/*
{
    "type": "Content",
    "template" : "Left: {} - Right: {}",
    "vars" : [
                {
                    "memstart": "0x1165A",
                    "length" : 4,
                    "type" : "ascii_high"
                },
                {
                    "memstart": "0x1165E",
                    "length" : 4,
                    "type" : "ascii_high"
                }
            ]
}
*/

bool SidebarContent::UpdateBlock(SidebarManager* sbM, UINT8 sidebarId, UINT8 blockId, nlohmann::json* pdata)
{
    nlohmann::json data = *pdata;
    // OutputDebugStringA((data.dump()+string("\n")).c_str());

    try
    {
        Sidebar sb = sbM->sidebars.at(sidebarId);
        auto b = sb.blocks.at(blockId);
        string s;
        switch (b->type)
        {
        case BlockType::Empty:
            return true;
            break;
        case BlockType::Header:
            s = FormatBlockText(pdata);
            break;
        default:                    // Content
            s = FormatBlockText(pdata);
            break;
        }

        // OutputDebugStringA(s.c_str());
        // OutputDebugStringA("\n");
        if (sb.SetBlockText(s, blockId) == SidebarError::ERR_NONE)
        {
            return true;
        }
        return false;
    }
    catch (...)
    {
        return false;
    }
}

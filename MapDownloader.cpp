#include <iostream>
#include <string>
#include <windows.h>
#include <fstream>
#include <vector>

#include "bzip2/bzlib.h"

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

#define VERSION "1.0"
#define FASTDL_MAPS_EU "http://eu.n5srv.ru.net/n5/fastdl/zs/maps/%s.bsp.bz2"
#define FASTDL_MAPS_RU "http://ext.n5srv.ru.net/n5/fastdl/zs/maps/%s.bsp.bz2"
#define SERVER_MAPLIST "http://n5srv.ru.net/n5/maplist_%s.txt"

const char* tempfile = "temp.dat";
char curent_map[256] = {0};
bool useRUserver = true;

struct SMapInfo {
    char name[128];
    int size;
    int pksize;
};

inline bool file_exists(const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

bool file_correct(const std::string& filepath, SMapInfo* mapinfo) {
    /// TODO: check CRC32
    std::ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
    return ((int)in.tellg() == mapinfo->size);
}

void SetStatus(const char* status, bool end = false) {
    const char* whitespace = "               "; // kinda shitcode
    if (end) {
        std::cout << curent_map << "(" << status << ")" << whitespace << std::endl;
    } else {
        std::cout << curent_map << "(" << status << ")" << whitespace << '\r';
    }
}

const int OutBufSize = 65536;
char buf[OutBufSize];

bool DownloadMap(std::string& mapname, std::string& dstfile) {
    char mapurl[256];
    sprintf(mapurl, useRUserver ? FASTDL_MAPS_RU : FASTDL_MAPS_EU, mapname.c_str());

    SetStatus("Downloading...");
 
    if (S_OK != URLDownloadToFileA(NULL, mapurl, tempfile, 0, NULL)) {
        SetStatus("Failed to download!", true);
        return false;
    }

    SetStatus("Decompressing...");

    BZFILE* bzfp = BZ2_bzopen(tempfile, "rb");
    if (bzfp == NULL) {
        SetStatus("Failed to decompress!", true);
        return false;
    }

    std::ofstream outfile(dstfile, std::ofstream::binary);

    while (true) {
        int bytesRead = BZ2_bzread(bzfp, buf, OutBufSize);
        if (bytesRead < 0) {
            break;
        }

        if (bytesRead > 0) {
            if (!outfile.write(buf, bytesRead)) {
                break;
            }
        } else {
            outfile.close();
            BZ2_bzclose(bzfp);
            SetStatus("Done", true);
            return true;
        }
    }

    outfile.close();
    BZ2_bzclose(bzfp);
    SetStatus("Not decompressed!", true);
    return false;
}

bool GetGamePath(std::string& path) {
    HKEY hKey;
    LONG lResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Wow6432Node\\Valve\\Steam", 0, KEY_READ, &hKey);
    if (lResult != ERROR_SUCCESS) {
        lResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hKey);
        if (lResult != ERROR_SUCCESS) {
            std::cout << "Failed to open registry key" << std::endl;
            return false;
        }
    }

    char steamPath[MAX_PATH];
    DWORD pathSize = sizeof(steamPath);
    lResult = RegQueryValueExA(hKey, "InstallPath", 0, NULL, (LPBYTE)steamPath, &pathSize);
    if (lResult != ERROR_SUCCESS) {
        std::cout << "Failed to query registry value" << std::endl;
        RegCloseKey(hKey);
        return false;
    }

    RegCloseKey(hKey);
        
    path = std::string(steamPath) + "\\steamapps\\common\\GarrysMod\\";
    
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cout << "GarrysMod folder not found" << std::endl;
        return false;
    }

    return true;
}

int main() {
    std::string gamepath;
    size_t remainmbytes = 0;

    std::cout << "N5 MapDownloader v" << VERSION << std::endl;

    // get GMod maps path
    if (!GetGamePath(gamepath)) {
        return 1;
    }

    gamepath.append("garrysmod\\download\\maps\\");
    std::cout << "Download path: " + gamepath << std::endl;
    
    // get list of maps
    useRUserver = (MessageBox(NULL, L"Использовать RU сервер? (рекомендуется)", L"N5 MapDownloader", MB_YESNO) == IDYES);
    std::cout << "Server: " << (useRUserver ? "RU" : "EU") << std::endl;

    bool shouldloadall = (MessageBox(NULL, L"Загружать только используемые карты?\nЕсли нажать Нет - загрузятся все карты, которые есть на сервере.", L"N5 MapDownloader", MB_YESNO) == IDNO);
    
    char maplist_url[256];
    sprintf(maplist_url, SERVER_MAPLIST, shouldloadall ? "all" : "allowed");

    if (S_OK != URLDownloadToFileA(NULL, maplist_url, tempfile, 0, NULL)) {
        std::cout << "Failed to download maplist from " << maplist_url << std::endl;
        return 1;
    }

    std::vector <SMapInfo*> maplist;
    std::ifstream file(tempfile);
    if (!file.is_open()) {
        std::cout << "Failed to open " << tempfile << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        SMapInfo* mapinfo = new SMapInfo;
        if (sscanf(line.c_str(), "%s %i %i", mapinfo->name, &mapinfo->size, &mapinfo->pksize)) {
            std::string dstfile = gamepath + mapinfo->name + ".bsp";
            if (file_exists(dstfile) && file_correct(dstfile, mapinfo)) {
                // skiping
            }
            else {
                maplist.push_back(mapinfo);
                remainmbytes += (mapinfo->pksize / 1048576);
            }
        }
    }
    file.close();

    // downloading maps
    std::cout << std::endl;
    int count = 0;
    for (auto& mapinfo : maplist) {
        std::string mapname = mapinfo->name;
        std::string dstfile = gamepath + mapname + ".bsp";

        sprintf(curent_map, "(%i %iMB) %s ", (int)maplist.size() - count, ((int)remainmbytes), mapname.c_str());

        DownloadMap(mapname, dstfile);
        remainmbytes -= (mapinfo->pksize / 1048576);
        count += 1;
    }

    std::remove(tempfile);

    std::cout << "Complete!" << std::endl;

    return 0;
}
/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Banner.h"
#include "Config.h"
#include "DB2FileLoader.h"
#include "DB2FileSystemSource.h"
#include "ExtractorDB2LoadInfo.h"
#include "MapBuilder.h"
#include "PathCommon.h"
#include "Timer.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include <boost/filesystem/operations.hpp>
#include <filesystem>
#include <unordered_map>
#include <vector>

using namespace MMAP;

namespace
{
    std::unordered_map<uint32, uint8> _liquidTypes;
}

bool checkDirectories(bool debugOutput, std::vector<std::string>& dbcLocales)
{
    if (getDirContents(dbcLocales, "dbc") == LISTFILE_DIRECTORY_NOT_FOUND || dbcLocales.empty())
    {
        printf("'dbc' directory is empty or does not exist\n");
        return false;
    }

    std::vector<std::string> dirFiles;

    if (getDirContents(dirFiles, "maps") == LISTFILE_DIRECTORY_NOT_FOUND || dirFiles.empty())
    {
        printf("'maps' directory is empty or does not exist\n");
        return false;
    }

    dirFiles.clear();
    if (getDirContents(dirFiles, "vmaps", "*.vmtree") == LISTFILE_DIRECTORY_NOT_FOUND || dirFiles.empty())
    {
        printf("'vmaps' directory is empty or does not exist\n");
        return false;
    }

    dirFiles.clear();
    if (getDirContents(dirFiles, "mmaps") == LISTFILE_DIRECTORY_NOT_FOUND)
    {
        if (!boost::filesystem::create_directory("mmaps"))
        {
            printf("'mmaps' directory does not exist and failed to create it\n");
            return false;
        }
    }

    dirFiles.clear();
    if (debugOutput)
    {
        if (getDirContents(dirFiles, "meshes") == LISTFILE_DIRECTORY_NOT_FOUND)
        {
            if (!boost::filesystem::create_directory("meshes"))
            {
                printf("'meshes' directory does not exist and failed to create it (no place to put debugOutput files)\n");
                return false;
            }
        }
    }

    return true;
}

bool handleArgs(int argc, char** argv,
               int& mapnum,
               int& tileX,
               int& tileY,
               std::string& configFilePath,
               bool& silent,
               char*& offMeshInputPath,
               char*& file,
               unsigned int& threads)
{
    bool hasCustomConfigPath = false;
    char* param = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--config") == 0)
        {
            param = argv[++i];
            if (!param)
                return false;

            hasCustomConfigPath = true;
            configFilePath = param;
        }
        else if (strcmp(argv[i], "--threads") == 0)
        {
            param = argv[++i];
            if (!param)
                return false;
            threads = static_cast<unsigned int>(std::max(0, atoi(param)));
        }
        else if (strcmp(argv[i], "--file") == 0)
        {
            param = argv[++i];
            if (!param)
                return false;
            file = param;
        }
        else if (strcmp(argv[i], "--tile") == 0)
        {
            param = argv[++i];
            if (!param)
                return false;

            char* stileX = strtok(param, ",");
            char* stileY = strtok(nullptr, ",");
            int tilex = atoi(stileX);
            int tiley = atoi(stileY);

            if ((tilex > 0 && tilex < 64) || (tilex == 0 && strcmp(stileX, "0") == 0))
                tileX = tilex;
            if ((tiley > 0 && tiley < 64) || (tiley == 0 && strcmp(stileY, "0") == 0))
                tileY = tiley;

            if (tileX < 0 || tileY < 0)
            {
                printf("invalid tile coords.\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--silent") == 0)
        {
            silent = true;
        }
        else if (strcmp(argv[i], "--offMeshInput") == 0)
        {
            param = argv[++i];
            if (!param)
                return false;

            offMeshInputPath = param;
        }
        else
        {
            int map = atoi(argv[i]);
            if (map > 0 || (map == 0 && (strcmp(argv[i], "0") == 0)))
                mapnum = map;
            else
            {
                printf("invalid map id\n");
                return false;
            }
        }
    }

    if (!hasCustomConfigPath)
    {
        FILE* f = fopen(configFilePath.c_str(), "r");
        if (!f)
        {
            auto execRelPath = std::filesystem::path(executableDirectoryPath()) / configFilePath;
            f = fopen(execRelPath.string().c_str(), "r");
            if (!f)
            {
                printf("Failed to load configuration. Ensure that 'mmaps-config.yaml' exists in the current directory or specify its path using the --config option.\n");
                return false;
            }
            configFilePath = execRelPath.string();
        }
        fclose(f);
    }

    return true;
}

int finish(const char* message, int returnValue)
{
    printf("%s", message);
    getchar(); // Wait for user input
    return returnValue;
}

std::unordered_map<uint32, uint8> LoadLiquid(std::string const& locale)
{
    DB2FileLoader liquidDb2;
    std::unordered_map<uint32, uint8> liquidData;
    DB2FileSystemSource liquidTypeSource((boost::filesystem::path("dbc") / locale / "LiquidType.db2").string());
    if (liquidDb2.Load(&liquidTypeSource, LiquidTypeLoadInfo::Instance()))
    {
        for (uint32 x = 0; x < liquidDb2.GetRecordCount(); ++x)
        {
            DB2Record record = liquidDb2.GetRecord(x);
            liquidData[record.GetId()] = record.GetUInt8("SoundBank");
        }
    }

    return liquidData;
}

std::unordered_map<uint32, std::vector<uint32>> LoadMap(std::string const& locale)
{
    DB2FileLoader mapDb2;
    std::unordered_map<uint32, std::vector<uint32>> mapData;
    DB2FileSystemSource mapSource((boost::filesystem::path("dbc") / locale / "Map.db2").string());
    if (mapDb2.Load(&mapSource, MapLoadInfo::Instance()))
    {
        for (uint32 x = 0; x < mapDb2.GetRecordCount(); ++x)
        {
            DB2Record record = mapDb2.GetRecord(x);
            mapData.emplace(std::piecewise_construct, std::forward_as_tuple(record.GetId()), std::forward_as_tuple());
            int16 parentMapId = int16(record.GetUInt16("ParentMapID"));
            if (parentMapId != -1)
                mapData[parentMapId].push_back(record.GetId());
        }
    }

    return mapData;
}

int main(int argc, char** argv)
{
    Destiny::Banner::Show("MMAP generator", [](char const* text) { printf("%s\n", text); }, nullptr);

    unsigned int threads = std::thread::hardware_concurrency();
    int mapnum = -1;
    int tileX = -1, tileY = -1;
    bool silent = false;
    char* offMeshInputPath = nullptr;
    char* file = nullptr;
    std::string configFilePath = "mmaps-config.yaml";

    bool validParam = handleArgs(argc, argv, mapnum,
                                 tileX, tileY, configFilePath, silent, offMeshInputPath, file, threads);

    if (!validParam)
        return silent ? -1 : finish("You have specified invalid parameters", -1);

    auto config = Config::FromFile(configFilePath);
    if (!config)
        return silent ? -1 : finish("Failed to load configuration. Ensure that 'mmaps-config.yaml' exists in the current directory or specify its path using the --config option.", -1);

    if (mapnum == -1 && config->IsDebugOutputEnabled())
    {
        if (silent)
            return -2;

        printf("You have specified debug output, but didn't specify a map to generate.\n");
        printf("This will generate debug output for ALL maps.\n");
        printf("Are you sure you want to continue? (y/n) ");
        if (getchar() != 'y')
            return 0;
    }

    // Switch working directory to the resolved data dir so that subsequent
    // relative paths ("dbc", "maps", "vmaps", "mmaps", "meshes") work as before.
    std::error_code ec;
    std::filesystem::current_path(config->DataDirPath(), ec);
    if (ec)
        return silent ? -3 : finish(("Failed to chdir to data dir: " + ec.message() + "\n").c_str(), -3);

    std::vector<std::string> dbcLocales;
    if (!checkDirectories(config->IsDebugOutputEnabled(), dbcLocales))
        return silent ? -3 : finish("Press ENTER to close...", -3);

    _liquidTypes = LoadLiquid(dbcLocales[0]);
    if (_liquidTypes.empty())
        return silent ? -5 : finish("Failed to load LiquidType.db2", -5);

    std::unordered_map<uint32, std::vector<uint32>> mapData = LoadMap(dbcLocales[0]);
    if (mapData.empty())
        return silent ? -4 : finish("Failed to load Map.db2", -4);

    static_cast<VMAP::VMapManager2*>(VMAP::VMapFactory::createOrGetVMapManager())->InitializeThreadUnsafe(mapData);
    static_cast<VMAP::VMapManager2*>(VMAP::VMapFactory::createOrGetVMapManager())->GetLiquidFlagsPtr = [](uint32 liquidId) -> uint32
    {
        auto itr = _liquidTypes.find(liquidId);
        return itr != _liquidTypes.end() ? (1 << itr->second) : 0;
    };

    MapBuilder builder(&config.value(), mapnum, offMeshInputPath);

    uint32 start = getMSTime();
    if (file)
        builder.buildMeshFromFile(file);
    else if (tileX > -1 && tileY > -1 && mapnum >= 0)
        builder.buildSingleTile(mapnum, tileX, tileY);
    else if (mapnum >= 0)
        builder.buildMap(uint32(mapnum));
    else
        builder.buildAllMaps(threads);

    VMAP::VMapFactory::clear();

    if (!silent)
        printf("Finished. MMAPS were built in %u ms!\n", GetMSTimeDiffToNow(start));
    return 0;
}

/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
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

#ifndef _MMAP_CONFIG_H
#define _MMAP_CONFIG_H

#include "Define.h"
#include "MapDefines.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace std
{
    template <>
    struct hash<std::pair<uint32_t, uint32_t>>
    {
        std::size_t operator()(const std::pair<uint32_t, uint32_t>& p) const noexcept
        {
            return std::hash<uint64_t>()((static_cast<uint64_t>(p.first) << 32) | p.second);
        }
    };
}

namespace MMAP
{
    struct ResolvedMeshConfig
    {
        float walkableSlopeAngle;
        int walkableRadius;
        int walkableHeight;
        int walkableClimb;
        int vertexPerMapEdge;
        int vertexPerTileEdge;
        int tilesPerMapEdge;
        float baseUnitDim;
        float cellSizeHorizontal;
        float cellSizeVertical;
        float maxSimplificationError;

        MmapTileRecastConfig toMMAPTileRecastConfig() const;
    };

    class Config
    {
    public:
        static std::optional<Config> FromFile(std::string_view configFile);

        ~Config() = default;

        ResolvedMeshConfig GetConfigForTile(uint32 mapID, uint32 tileX, uint32 tileY) const;

        bool ShouldSkipLiquid() const { return _skipLiquid; }
        bool ShouldSkipContinents() const { return _skipContinents; }
        bool ShouldSkipJunkMaps() const { return _skipJunkMaps; }
        bool ShouldSkipBattlegrounds() const { return _skipBattlegrounds; }
        bool IsDebugOutputEnabled() const { return _debugOutput; }

        std::string VMapsPath() const { return (_dataDir / "vmaps").string(); }
        std::string MapsPath() const { return (_dataDir / "maps").string(); }
        std::string MMapsPath() const { return (_dataDir / "mmaps").string(); }
        std::string DataDirPath() const { return _dataDir.string(); }

    private:
        explicit Config();

        bool LoadConfig(std::string_view configFile);

        struct TileOverride
        {
            std::optional<float> walkableSlopeAngle;
            std::optional<int> walkableRadius;
            std::optional<int> walkableHeight;
            std::optional<int> walkableClimb;
        };

        struct MapOverride
        {
            std::optional<float> walkableSlopeAngle;
            std::optional<int> walkableRadius;
            std::optional<int> walkableHeight;
            std::optional<int> walkableClimb;
            std::optional<int> vertexPerMapEdge;
            std::optional<int> vertexPerTileEdge;
            std::optional<float> cellSizeHorizontal;
            std::optional<float> cellSizeVertical;

            std::unordered_map<std::pair<uint32, uint32>, TileOverride> tileOverrides;
        };

        struct GlobalConfig
        {
            float walkableSlopeAngle = 70.0f;
            int walkableRadius = 2;
            int walkableHeight = 6;
            int walkableClimb = 4;
            int vertexPerMapEdge = 2000;
            int vertexPerTileEdge = 80;
            float maxSimplificationError = 1.8f;
        };

        GlobalConfig _global;
        std::unordered_map<uint32, MapOverride> _maps;

        bool _skipLiquid       = false;
        bool _skipContinents   = false;
        bool _skipJunkMaps     = true;
        bool _skipBattlegrounds = false;
        bool _debugOutput      = false;

        std::filesystem::path _dataDir;
    };
}

#endif // _MMAP_CONFIG_H

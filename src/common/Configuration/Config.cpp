/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "Config.h"
#include "Log.h"
#include "Util.h"
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <algorithm>
#include <memory>
#include <mutex>

namespace bpt = boost::property_tree;
namespace fs = boost::filesystem;

namespace
{
    std::string _filename;
    std::vector<std::string> _args;
    bpt::ptree _config;
    std::mutex _configLock;

    // Merges the keys of the first section of the given ini file into _config,
    // overriding any existing entry. Module configs reuse the [worldserver]
    // section like the main config, so we only ever read the first section.
    // The caller must hold _configLock.
    bool MergeAdditionalFile(std::string const& file)
    {
        try
        {
            bpt::ptree additionalTree;
            bpt::ini_parser::read_ini(file, additionalTree);

            if (additionalTree.empty())
                return false;

            // '\01' as separator keeps dotted keys (e.g. "Module.Enable") as a
            // single node, matching how the main config is read back.
            for (bpt::ptree::value_type const& key : additionalTree.begin()->second)
                _config.put(bpt::ptree::path_type(key.first, '\01'), key.second.data());
        }
        catch (bpt::ini_parser::ini_parser_error const& e)
        {
            if (e.line() == 0)
                TC_LOG_ERROR("server.loading", "Error in module config %s", e.message().c_str());
            else
                TC_LOG_ERROR("server.loading", "Error in module config %s:%u - %s",
                    e.filename().c_str(), uint32(e.line()), e.message().c_str());
            return false;
        }

        return true;
    }
}

bool ConfigMgr::LoadInitial(std::string const& file, std::vector<std::string> args,
                            std::string& error)
{
    std::lock_guard<std::mutex> lock(_configLock);

    _filename = file;
    _args = args;

    try
    {
        bpt::ptree fullTree;
        bpt::ini_parser::read_ini(file, fullTree);

        if (fullTree.empty())
        {
            error = "empty file (" + file + ")";
            return false;
        }

        // Since we're using only one section per config file, we skip the section and have direct property access
        _config = fullTree.begin()->second;
    }
    catch (bpt::ini_parser::ini_parser_error const& e)
    {
        if (e.line() == 0)
            error = e.message() + " (" + e.filename() + ")";
        else
            error = e.message() + " (" + e.filename() + ":" + std::to_string(e.line()) + ")";
        return false;
    }

    return true;
}

ConfigMgr* ConfigMgr::instance()
{
    static ConfigMgr instance;
    return &instance;
}

bool ConfigMgr::LoadModulesConfigs(bool isReload /*= false*/)
{
    std::lock_guard<std::mutex> lock(_configLock);

    // Module configs live in a "modules" directory next to the main config file.
    fs::path const modulesPath = fs::path(_filename).parent_path() / "modules";

    boost::system::error_code ec;
    if (!fs::is_directory(modulesPath, ec))
        return true;

    std::vector<std::string> loadedFiles;
    for (fs::directory_iterator itr(modulesPath, ec), end; itr != end && !ec; itr.increment(ec))
    {
        fs::path const& file = itr->path();
        if (file.extension() != ".conf")
            continue;

        if (MergeAdditionalFile(file.string()))
            loadedFiles.push_back(file.filename().string());
    }

    if (!isReload && !loadedFiles.empty())
    {
        TC_LOG_INFO("server.loading", "Using modules configuration:");
        for (std::string const& name : loadedFiles)
            TC_LOG_INFO("server.loading", "> %s", name.c_str());
    }

    return true;
}

bool ConfigMgr::Reload(std::string& error)
{
    if (!LoadInitial(_filename, std::move(_args), error))
        return false;

    return LoadModulesConfigs(true);
}

template<class T>
T ConfigMgr::GetValueDefault(std::string const& name, T def) const
{
    try
    {
        return _config.get<T>(bpt::ptree::path_type(name, '/'));
    }
    catch (bpt::ptree_bad_path)
    {
        TC_LOG_WARN("server.loading", "Missing name %s in config file %s, add \"%s = %s\" to this file",
            name.c_str(), _filename.c_str(), name.c_str(), std::to_string(def).c_str());
    }
    catch (bpt::ptree_bad_data)
    {
        TC_LOG_ERROR("server.loading", "Bad value defined for name %s in config file %s, going to use %s instead",
            name.c_str(), _filename.c_str(), std::to_string(def).c_str());
    }

    return def;
}

template<>
std::string ConfigMgr::GetValueDefault<std::string>(std::string const& name, std::string def) const
{
    try
    {
        return _config.get<std::string>(bpt::ptree::path_type(name, '/'));
    }
    catch (bpt::ptree_bad_path)
    {
        TC_LOG_WARN("server.loading", "Missing name %s in config file %s, add \"%s = %s\" to this file",
            name.c_str(), _filename.c_str(), name.c_str(), def.c_str());
    }
    catch (bpt::ptree_bad_data)
    {
        TC_LOG_ERROR("server.loading", "Bad value defined for name %s in config file %s, going to use %s instead",
            name.c_str(), _filename.c_str(), def.c_str());
    }

    return def;
}

std::string ConfigMgr::GetStringDefault(std::string const& name, const std::string& def) const
{
    std::string val = GetValueDefault(name, def);
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
    return val;
}

bool ConfigMgr::GetBoolDefault(std::string const& name, bool def) const
{
    std::string val = GetValueDefault(name, std::string(def ? "1" : "0"));
    val.erase(std::remove(val.begin(), val.end(), '"'), val.end());
    return StringToBool(val);
}

int ConfigMgr::GetIntDefault(std::string const& name, int def) const
{
    return GetValueDefault(name, def);
}

float ConfigMgr::GetFloatDefault(std::string const& name, float def) const
{
    return GetValueDefault(name, def);
}

std::string const& ConfigMgr::GetFilename()
{
    std::lock_guard<std::mutex> lock(_configLock);
    return _filename;
}

std::vector<std::string> const& ConfigMgr::GetArguments() const
{
    return _args;
}

std::vector<std::string> ConfigMgr::GetKeysByString(std::string const& name)
{
    std::lock_guard<std::mutex> lock(_configLock);

    std::vector<std::string> keys;

    for (bpt::ptree::value_type const& child : _config)
        if (child.first.compare(0, name.length(), name) == 0)
            keys.push_back(child.first);

    return keys;
}

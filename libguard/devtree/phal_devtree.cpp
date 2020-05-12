// SPDX-License-Identifier: Apache-2.0
#include "phal_devtree.hpp"

#include "guard_log.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace openpower
{
namespace guard
{
namespace phal
{
namespace log = openpower::guard::log;

/**
 * Used to return in callback function which are used to get
 * physical path value and it binary format value.
 *
 * The value for constexpr defined based on pdbg_traverse function usage.
 */
constexpr int continueTgtTraversal = 0;
constexpr int requireAttrFound = 1;
constexpr int requireAttrNotFound = 2;

/**
 * Used to store|retrieve physical path in binary format inside pdbg
 * callback functions to get value from device tree
 */
static ATTR_PHYS_BIN_PATH_Type g_physBinaryPath;

/**
 * Used to store|retrieve physical path in string format inside pdbg
 * callback functions to get value from device tree
 */
static ATTR_PHYS_DEV_PATH_Type g_physStringPath;

void initPHAL()
{
    // Set log level to info
    pdbg_set_loglevel(PDBG_ERROR);

    /**
     * Passing fdt argument as NULL so, pdbg will use PDBG_DTB environment
     * variable to get system device tree.
     */
    if (!pdbg_targets_init(NULL))
    {
        log::guard_log(GUARD_ERROR, "pdbg_targets_init failed");
        throw std::runtime_error("pdbg target initialization failed");
    }
}

int pdbgCallbackToGetPhysicaBinaryPath(struct pdbg_target* target,
                                       void* /*priv - unused*/)
{
    ATTR_PHYS_DEV_PATH_Type physStringPath;
    if (DT_GET_PROP(ATTR_PHYS_DEV_PATH, target, physStringPath))
    {
        /**
         * Continue target traversal if ATTR_PHYS_DEV_PATH
         * attribute not found.
         */
        return continueTgtTraversal;
    }

    if (std::strcmp(g_physStringPath, physStringPath) != 0)
    {
        /**
         * Continue target traversal if ATTR_PHYS_DEV_PATH
         * attribute value is not matched with given physical path value.
         */
        return continueTgtTraversal;
    }
    else
    {
        /**
         * The ATTR_PHYS_DEV_PATH attribute value is matched with given
         * physical path value. so, getting physical path binary value
         * by using ATTR_PHYS_BIN_PATH attribute from same target property
         * list.
         */

        // Clear old value in g_physBinaryPath
        std::memset(g_physBinaryPath, 0, sizeof(g_physBinaryPath));

        if (DT_GET_PROP(ATTR_PHYS_BIN_PATH, target, g_physBinaryPath))
        {
            /**
             * Stopping the target target traversal if ATTR_PHYS_BIN_PATH
             * attribute not found within ATTR_PHYS_DEV_PATH attribute
             * target property list
             */
            return requireAttrNotFound;
        }
        else
        {
            /**
             * Found the physical binary value from device tree by using
             * given physical path value.
             */
            return requireAttrFound;
        }
    }

    return requireAttrNotFound;
}

std::optional<EntityPath>
    getEntityPathFromDevTree(const std::string& physicalPath)
{
    /**
     * Make sure physicalPath is in lower case because,
     * in device tree physical path is in lower case
     */
    std::string l_physicalPath(physicalPath);
    std::transform(l_physicalPath.begin(), l_physicalPath.end(),
                   l_physicalPath.begin(), ::tolower);

    /**
     * Removing "/" as prefix in given path if found to match with
     * device tree value
     */
    if (!l_physicalPath.compare(0, 1, "/"))
    {
        l_physicalPath.erase(0, 1);
    }

    /**
     * Adding "physical:" as prefix to given path to match with
     * device tree value if not given
     */
    if (l_physicalPath.find("physical:", 0, 9) == std::string::npos)
    {
        l_physicalPath.insert(0, "physical:");
    }

    if ((l_physicalPath.length() - 1 /* To include NULL terminator */) >
        sizeof(g_physStringPath))
    {
        log::guard_log(
            GUARD_ERROR,
            "Physical path size mismatch with given[%zu] and max size[%zu]",
            sizeof(g_physStringPath), (l_physicalPath.length() - 1));
        return std::nullopt;
    }

    /**
     * The callback function (pdbgCallbackToGetPhysicaBinaryPath) will use
     * the given physical path from g_physStringPath variable.
     * so, clear old value in g_physStringPath
     */
    std::memset(g_physStringPath, 0, sizeof(g_physStringPath));

    /**
     * The caller given value of physical path will be below format if not
     * in device tree format to get raw data of physical path.
     * E.g: physical:sys-0/node-0/proc-0
     */
    std::strncpy(g_physStringPath, l_physicalPath.c_str(),
                 sizeof(g_physStringPath));

    int ret = pdbg_traverse(
        nullptr /* Passing NULL to start target traversal from root */,
        pdbgCallbackToGetPhysicaBinaryPath,
        nullptr /* No application private data, so passing NULL */);
    if (ret == 0)
    {
        log::guard_log(
            GUARD_ERROR,
            "Given physical path not found in power system device tree");
        return std::nullopt;
    }
    else if (ret == requireAttrNotFound)
    {
        log::guard_log(
            GUARD_ERROR,
            "Binary value for given physical path is not found in device tree");
        return std::nullopt;
    }

    if (sizeof(EntityPath) < sizeof(g_physBinaryPath))
    {
        log::guard_log(
            GUARD_ERROR,
            "Physical path binary size mismatch with devtree[%zu] guard[%zu]",
            sizeof(g_physBinaryPath), sizeof(EntityPath));
        return std::nullopt;
    }

    /**
     * The assumption is, in given raw data first byte contains path type
     * and size of path elements as 2 x 4 bitfields representation.
     */
    EntityPath entityPath;
    entityPath.type_size = g_physBinaryPath[0];

    uint8_t pathElementsSize = (entityPath.type_size & 0x0F);
    if (pathElementsSize > EntityPath::maxPathElements)
    {
        log::guard_log(
            GUARD_ERROR,
            "Path elements size mismatch with devtree[%u] guard max size[%d]",
            pathElementsSize, EntityPath::maxPathElements);
        return std::nullopt;
    }

    for (int i = 0, j = 1 /* To ignore first byte (type_size) from raw data */;
         i < pathElementsSize; i++, j += sizeof(entityPath.pathElements[0]))
    {
        entityPath.pathElements[i].targetType = g_physBinaryPath[j];
        entityPath.pathElements[i].instance = g_physBinaryPath[j + 1];
    }

    return entityPath;
}
} // namespace phal
} // namespace guard
} // namespace openpower
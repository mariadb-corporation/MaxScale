/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>
#include <vector>
#include <libxml/tree.h>

namespace maxbase
{

namespace xml
{

/**
 * @brief Get the fully qualified name of a node.
 *
 * @param node  The node.
 *
 * @return The qualified name of the node.
 */
std::string get_qualified_name(const xmlNode& node);

/**
 * Find descendant nodes corresponding to particular xpath.
 *
 * @param ancestor  The node to use as root when searching.
 * @param zXpath    The xpath, defined relative to @c ancestor. Before
 *                  used, the xpath will be prepended with "./".
 *
 * @return The descendants corresponding to the xpath.
 */
std::vector<xmlNode*> find_descendants_by_xpath(xmlNode& ancestor, const char* zXpath);

inline std::vector<xmlNode*> find_descendants_by_xpath(xmlNode& ancestor, const std::string& xpath)
{
    return find_descendants_by_xpath(ancestor, xpath.c_str());
}

/**
 * Find descendant node corresponding to particular xpath.
 *
 * @note Should only be used with an xpath that can only identify a single node.
 *
 * @param ancestor  The node to use as root when searching.
 * @param zXpath    The xpath, defined relative to @c ancestor. Before
 *                  used, the xpath will be prepended with "./".
 *
 * @return The descendant corresponding to the xpath, or NULL if none do.
 */
xmlNode* find_descendant_by_xpath(xmlNode& ancestor, const char* zXpath);

inline xmlNode* find_descendant_by_xpath(xmlNode& ancestor, const std::string& xpath)
{
    return find_descendant_by_xpath(ancestor, xpath.c_str());
}

/**
 * Find immediate children whose names begins with a certain prefix.
 *
 * @param parent   The parent node.
 * @param zPrefix  The prefix the name of a child should have to be included.
 *
 * @return Children that fulfill the requirement.
 */
std::vector<xmlNode*> find_children_by_prefix(xmlNode& parent, const char* zPrefix);

inline std::vector<xmlNode*> find_children_by_prefix(xmlNode& parent, const std::string& prefix)
{
    return find_children_by_prefix(parent, prefix.c_str());
}

/**
 * Find descendant by path.
 *
 * @param ancestor  The node to start from.
 * @param zPath     The path to the descendant.
 *
 * @return The descendant or NULL if it does not exist.
 */
xmlNode* find_descendant(xmlNode& ancestor, const char* zPath);

inline xmlNode* find_descendant(xmlNode& ancestor, const std::string& path)
{
    return find_descendant(ancestor, path.c_str());
}

/**
 * Find child by name.
 *
 * @param parent  The node to start from.
 * @param zName   The name of the child.
 *
 * @return The child or NULL if it does not exist.
 */

xmlNode* find_child(xmlNode& parent, const char* zName);

inline xmlNode* find_child(xmlNode& ancestor, const std::string& name)
{
    return find_child(ancestor, name.c_str());
}

}
}

/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <libxml/tree.h>

namespace std
{
template<>
class default_delete<xmlDoc>
{
public:
    void operator()(xmlDoc* pDoc)
    {
        xmlFreeDoc(pDoc);
    }
};
}

namespace maxbase
{

namespace xml
{

/**
 * @brief Compile textual XML into an XML document object.
 *
 * @param zXml The XML text.
 * @param len  The length of the text.
 * @param url  Base URL of the document.
 *
 * @param A XML document object or NULL if the text could not be compiled.
 */
std::unique_ptr<xmlDoc> load(const char* zXml, size_t len, const std::string& url = "noname.xml");

inline std::unique_ptr<xmlDoc> load(const char* zXml, const std::string& url = "noname.xml")
{
    return load(zXml, strlen(zXml), url);
}

inline std::unique_ptr<xmlDoc> load(const std::string& xml, const std::string& url = "noname.xml")
{
    return load(xml.c_str(), xml.length(), url);
}

/**
 * @brief Get the content of a node.
 *
 * @param node  The node whose content to get.
 *
 * @return The content.
 */
std::string get_content(const xmlNode& node);

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

enum class XmlLocation
{
    AT_BEGINNING,
    AT_END
};

/**
 * Insert new key/value to XML document.
 *
 * @param ancestor  The ancestor node.
 * @param zPath     If not only a name, the hierarchy starting at @c ancestor is first traversed.
 * @param zValue    The value.
 * @param location  Where the element should be added.
 *
 * @return True, if the key/value could be added. A return value of false
 *         means that a path was specified, but the beginning path did not exist.
 */
bool insert(xmlNode& parent,
            const char* zKey,
            const char* zValue,
            XmlLocation location = XmlLocation::AT_BEGINNING);

/**
 * Update value of key(s) in XML document.
 *
 * @param node        The node to use as root.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value);

/**
 * Update value of key(s) in XML document.
 *
 * @param csXml       The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must *NOT* be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if_not(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value = nullptr);

/**
 * Update value of key(s) in XML document.
 *
 * @param node        The node to use as root.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
inline int update(xmlNode& node, const char* zXpath, const char* zNew_value)
{
    return update_if(node, zXpath, zNew_value, nullptr);
}

/**
 * Update or insert a key/value to XML node.
 *
 * @param ancestor  An XML node.
 * @param zPath     The path (not xpath) identifying the element.
 * @param zValue    The value.
 * @param location  If inserted, where the element should be added.
 *
 * @return True, if the element could be updated or inserted. A return value
 *         of false means that the path did not identify an existing element
 *         and that the beginning path did not exist, so the element could not be
 *         added either.
 */
bool upsert(xmlNode& ancestor,
            const char* zPath,
            const char* zValue,
            XmlLocation location = XmlLocation::AT_BEGINNING);

/**
 * Remove key(s)
 *
 * @param node    The node to use as root.
 * @param zXpath  The XML path identifying the key(s).
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of removed keys.
 */
int remove(xmlNode& node, const char* zXPath);

/**
 * Convert XML document to a string.
 *
 * @param doc  XML document to dump to a string.
 *
 * @return The XML document as a string.
 */
std::string dump(const xmlDoc& doc);

}
}

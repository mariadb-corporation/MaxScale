/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "csxml.hh"
#include <sstream>

using namespace std;

template<>
long mxb::xml::get_content_as<long>(const xmlChar* pContent)
{
    const char* zContent = reinterpret_cast<const char*>(pContent);
    char* zEnd;
    errno = 0;
    long l = strtol(zContent, &zEnd, 10);

    bool valid = (errno == 0 && zEnd != zContent && *zEnd == 0);

    if (!valid)
    {
        stringstream ss;
        ss << "The content '" << zContent << "' cannot be turned into a long.";
        throw Exception(ss.str());
    }

    return l;
}

xmlNode& mxb::xml::get_descendant(xmlNode& ancestor,
                                  const char* zPath)
{
    xmlNode* pDescendant = find_descendant(ancestor, zPath);

    if (!pDescendant)
    {
        stringstream ss;
        ss << "The node '" << get_qualified_name(ancestor) << "' "
           << "does not have a descendant node '" << zPath << "'.";

        throw Exception(ss.str());
    }

    return *pDescendant;
}

xmlNode& mxb::xml::get_descendant_by_xpath(xmlNode& ancestor, const char* zXpath)
{
    xmlNode* pDescendant = nullptr;

    vector<xmlNode*> nodes = find_descendants_by_xpath(ancestor, zXpath);

    if (nodes.size() == 1)
    {
        pDescendant = nodes.front();
    }
    else if (nodes.size() == 0)
    {
        stringstream ss;
        ss << "The xpath '" << zXpath << "' does not identify a descendant for "
           << "the node '" << get_qualified_name(ancestor) << "'";

        throw Exception(ss.str());
    }
    else
    {
        stringstream ss;
        ss << "The xpath '" << zXpath << "' identifies " << nodes.size() << " "
           << "descendants for the node '" << get_qualified_name(ancestor) << "', "
           << "only one allowed.";

        throw Exception(ss.str());
    }

    return *pDescendant;
}


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
#include <maxbase/xml.hh>
#include <cstring>
#include <libxml/xpath.h>
#include <maxbase/assert.h>

using namespace std;

string mxb::xml::get_qualified_name(const xmlNode& node)
{
    string s(reinterpret_cast<const char*>(node.name));

    const xmlNode* pParent = node.parent;

    while (pParent)
    {
        s = string(reinterpret_cast<const char*>(pParent->name)) + "/" + s;
        pParent = pParent->parent;
    }

    return s;
}

vector<xmlNode*> mxb::xml::find_descendants_by_xpath(xmlNode& ancestor, const char* zXpath)
{
    vector<xmlNode*> descendants;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(ancestor.doc);
    mxb_assert(pXpath_context);

    string path(zXpath);
    path = "./" + path;
    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&ancestor,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     pXpath_context);


    xmlNodeSet* pNodes = pXpath_object->nodesetval;

    for (int i = 0; i < pNodes->nodeNr; ++i)
    {
        descendants.push_back(pNodes->nodeTab[i]);
    }

    xmlXPathFreeObject(pXpath_object);
    xmlXPathFreeContext(pXpath_context);

    return descendants;
}

xmlNode* mxb::xml::find_descendant_by_xpath(xmlNode& ancestor, const char* zXpath)
{
    vector<xmlNode*> descendants = find_descendants_by_xpath(ancestor, zXpath);
    mxb_assert(descendants.empty() || descendants.size() == 1);

    return descendants.empty() ? nullptr : descendants.front();
}

vector<xmlNode*> mxb::xml::find_children_by_prefix(xmlNode& parent, const char* zPrefix)
{
    vector<xmlNode*> children;

    int n = strlen(zPrefix);
    xmlNode* pChild = parent.children;

    while (pChild)
    {
        if (strncmp(reinterpret_cast<const char*>(pChild->name), zPrefix, n) == 0)
        {
            children.push_back(pChild);
        }

        pChild = pChild->next;
    }

    return children;
}

xmlNode* mxb::xml::find_descendant(xmlNode& ancestor, const char* zPath)
{
    xmlNode* pDescendant = nullptr;

    string path(zPath);

    auto pos = path.find_first_of("/");

    if (pos == string::npos)
    {
        pDescendant = find_child(ancestor, path);
    }
    else
    {
        string name = path.substr(0, pos);
        string tail = path.substr(pos + 1);

        xmlNode* pChild = find_child(ancestor, name);

        if (pChild)
        {
            pDescendant = find_descendant(*pChild, tail);
        }
    }

    return pDescendant;
}

xmlNode* mxb::xml::find_child(xmlNode& parent, const char* zName)
{
    xmlNode* pChild = parent.children;

    while (pChild && (strcmp(reinterpret_cast<const char*>(pChild->name), zName) != 0))
    {
        pChild = pChild->next;
    }

    return pChild;
}

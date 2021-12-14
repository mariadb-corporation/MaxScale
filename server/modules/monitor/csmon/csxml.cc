/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "csxml.hh"
#include <sstream>
#include <libxml/xpath.h>
#include <maxbase/assert.h>

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

namespace
{

bool xml_equal(const string& path,
               xmlNode& lhs, xmlXPathContext& lContext,
               xmlNode& rhs, xmlXPathContext& rContext,
               std::ostream* pErr);

bool xml_equal_children(const string& path,
                        xmlNode& lhs, xmlXPathContext& lContext,
                        xmlNode& rhs, xmlXPathContext& rContext,
                        std::ostream* pErr)
{
    bool rv = true;
    mxb_assert(strcmp(reinterpret_cast<const char*>(lhs.name),
                      reinterpret_cast<const char*>(rhs.name)) == 0);

    xmlNode* pL_child = lhs.children;

    if (pL_child
        && pL_child->type == XML_TEXT_NODE
        && !pL_child->next
        && !pL_child->children)
    {
        // Only one child that is text without children.
        xmlNode* pR_child = rhs.children;

        if (pR_child
            && pR_child->type == XML_TEXT_NODE
            && !pR_child->next
            && !pR_child->children)
        {
            // Also only one child that is text without children.
            auto* pL_content = xmlNodeGetContent(&lhs);
            auto* pR_content = xmlNodeGetContent(&rhs);

            const char* zL_content = reinterpret_cast<const char*>(pL_content);
            const char* zR_content = reinterpret_cast<const char*>(pR_content);

            if (zL_content && zR_content)
            {
                if (strcmp(zL_content, zR_content) != 0)
                {
                    if (pErr)
                    {
                        *pErr << path << "(L): " << zL_content << endl;
                        *pErr << path << "(R): " << zR_content << endl;
                    }
                    rv = false;
                }
            }
            else if (pL_content && !pR_content)
            {
                if (pErr)
                {
                    *pErr << path << "(L): " << zL_content << endl;
                    *pErr << path << "(R): NO CONTENT" << endl;
                }
                rv = false;
            }
            else if (pR_content && !pL_content)
            {
                if (pErr)
                {
                    *pErr << path << "(L): NO CONTENT" << endl;
                    *pErr << path << "(R): " << zR_content << endl;
                }
                rv = false;
            }
        }
        else
        {
            if (pErr)
            {
                *pErr << path << "(L): Single text node child." << endl;
                *pErr << path << "(R): NOT single text node child." << endl;
            }
            rv = false;
        }
    }
    else
    {
        // If we are producing output, we will not bail out at first error.
        while (pL_child && (rv || pErr))
        {
            if (pL_child->type == XML_ELEMENT_NODE)
            {
                mxb_assert(pL_child->name);

                string name(reinterpret_cast<const char*>(pL_child->name));
                string full_name = path + "/" + name;
                string xpath = "./" + name;
                const xmlChar* pXpath = reinterpret_cast<const xmlChar*>(xpath.c_str());

                xmlXPathObject* pXpath_object = xmlXPathNodeEval(&rhs, pXpath, &rContext);
                xmlNodeSet* pNodes = pXpath_object->nodesetval;
                mxb_assert(pNodes->nodeNr <= 1);

                if (pNodes->nodeNr == 0)
                {
                    if (pErr)
                    {
                        *pErr << "\"" << full_name << "\" found in first document, but not in other." << endl;
                    }
                    rv = false;
                }
                else
                {
                    mxb_assert(pNodes->nodeNr == 1);

                    xmlNode* pR_node = pNodes->nodeTab[0];

                    if (!xml_equal(full_name, *pL_child, lContext, *pR_node, rContext, pErr))
                    {
                        rv = false;
                    }
                }
            }

            pL_child = pL_child->next;
        }
    }

    return rv;
}

bool xml_equal(const string& path,
               xmlNode& lhs, xmlXPathContext& lContext,
               xmlNode& rhs, xmlXPathContext& rContext,
               std::ostream* pErr)
{
    mxb_assert(strcmp(reinterpret_cast<const char*>(lhs.name),
                      reinterpret_cast<const char*>(rhs.name)) == 0);

    bool rv = xml_equal_children(path, lhs, lContext, rhs, rContext, pErr);

    if (rv)
    {
        rv = xml_equal_children(path, rhs, rContext, lhs, lContext, pErr);
    }

    return rv;
}

}

bool mxb::xml::equal(const xmlNode& lhs, const xmlNode& rhs, std::ostream* pErr)
{
    bool rv = false;

    const char* zLeft_name = reinterpret_cast<const char*>(lhs.name);
    const char* zRight_name = reinterpret_cast<const char*>(rhs.name);

    if (strcmp(zLeft_name, zRight_name) == 0)
    {
        xmlXPathContext* pL_context = xmlXPathNewContext(lhs.doc);
        xmlXPathContext* pR_context = xmlXPathNewContext(rhs.doc);
        mxb_assert(pL_context && pR_context);

        rv = xml_equal(zLeft_name,
                       const_cast<xmlNode&>(lhs), *pL_context,
                       const_cast<xmlNode&>(rhs), *pR_context,
                       pErr);

        xmlXPathFreeContext(pR_context);
        xmlXPathFreeContext(pL_context);
    }
    else
    {
        if (pErr)
        {
            *pErr << zLeft_name << " != " << zRight_name << endl;
        }
    }

    return rv;
}

bool mxb::xml::equal(const xmlDoc& lhs, const xmlDoc& rhs, std::ostream* pErrors)
{
    xmlNode* pL = xmlDocGetRootElement(const_cast<xmlDoc*>(&lhs));
    xmlNode* pR = xmlDocGetRootElement(const_cast<xmlDoc*>(&rhs));

    mxb_assert(pL && pR);

    return equal(*pL, *pR, pErrors);
}

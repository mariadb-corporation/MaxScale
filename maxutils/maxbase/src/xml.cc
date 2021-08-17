/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/xml.hh>
#include <cstring>
#include <libxml/xpath.h>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>

using namespace std;

std::unique_ptr<xmlDoc> mxb::xml::load(const char* zXml, size_t len, const std::string& url)
{
    unique_ptr<xmlDoc> sDoc(xmlReadMemory(zXml, strlen(zXml), url.c_str(), NULL, 0));
    return sDoc;
}

string mxb::xml::get_content(const xmlNode& node)
{
    xmlChar* pContent = xmlNodeGetContent(const_cast<xmlNode*>(&node));
    string content(reinterpret_cast<const char*>(pContent));
    MXS_FREE(pContent);
    return content;
}

string mxb::xml::get_qualified_name(const xmlNode& node)
{
    string s(reinterpret_cast<const char*>(node.name));

    const xmlNode* pParent = node.parent;

    while (pParent && pParent->type != XML_DOCUMENT_NODE)
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

namespace
{

void xml_insert_leaf(xmlNode& parent, const char* zName, const char* zValue, mxb::xml::XmlLocation location)
{
    mxb_assert(strchr(zName, '/') == nullptr);

    xmlNode* pChild = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>(zName));
    xmlNode* pContent = xmlNewText(reinterpret_cast<const xmlChar*>(zValue));
    xmlAddChild(pChild, pContent);

    xmlNode* pSibling = parent.xmlChildrenNode;

    if (location == mxb::xml::XmlLocation::AT_BEGINNING && pSibling)
    {
        xmlAddPrevSibling(pSibling, pChild);
        xmlNode* pIndentation = xmlNewText(reinterpret_cast<const xmlChar*>("\n\t"));
        xmlAddPrevSibling(pChild, pIndentation);
    }
    else
    {
        xmlAddChild(&parent, pChild);
        if (pChild->prev
            && pChild->prev->type == XML_TEXT_NODE
            && strcmp(reinterpret_cast<char*>(xmlNodeGetContent(pChild->prev)), "\n") == 0)
        {
            xmlNodeSetContent(pChild->prev, reinterpret_cast<const xmlChar*>("\n\t"));
        }
        else
        {
            xmlNode* pIndentation = xmlNewText(reinterpret_cast<const xmlChar*>("\n\t"));
            xmlAddPrevSibling(pChild, pIndentation);
        }

        xmlNode* pLinebreak = xmlNewText(reinterpret_cast<const xmlChar*>("\n"));
        xmlAddNextSibling(pChild, pLinebreak);
    }
}

}

bool mxb::xml::insert(xmlNode& ancestor, const char* zPath, const char* zValue, XmlLocation location)
{
    mxb_assert(*zPath != '/');

    bool rv = false;

    string path(zPath);
    auto pos = path.find_last_of("/");

    if (pos == string::npos)
    {
        // zPath is a name and not a path.
        xml_insert_leaf(ancestor, zPath, zValue, location);
        rv = true;
    }
    else
    {
        string name = path.substr(pos + 1);
        string path = path.substr(0, pos);

        xmlNode* pParent = find_descendant(ancestor, path);

        if (pParent)
        {
            xml_insert_leaf(*pParent, name.c_str(), zValue, location);
            rv = true;
        }
    }

    return rv;
}

namespace
{

enum class UpdateWhen
{
    IF,
    IF_NOT
};

int xml_update(xmlNodeSet* pNodes, const char* zNew_value, const char* zIf_value, UpdateWhen update_when)
{
    int n = 0;
    int nNodes = pNodes ? pNodes->nodeNr : 0;

    // From the XML sample.
    /*
     * NOTE: the nodes are processed in reverse order, i.e. reverse document
     *       order because xmlNodeSetContent can actually free up descendant
     *       of the node and such nodes may have been selected too ! Handling
     *       in reverse order ensure that descendant are accessed first, before
     *       they get removed. Mixing XPath and modifications on a tree must be
     *       done carefully !
     */

    for (int i = nNodes - 1; i >= 0; --i)
    {
        const char* zValue = nullptr;
        auto* pNode = pNodes->nodeTab[i];

        if (zIf_value)
        {
            zValue = reinterpret_cast<const char*>(xmlNodeGetContent(pNode));
        }

        bool do_update = false;

        if (update_when == UpdateWhen::IF)
        {
            do_update = !zIf_value || (zValue && strcmp(zIf_value, zValue) == 0);
        }
        else
        {
            do_update = !zIf_value || (!zValue || strcmp(zIf_value, zValue) != 0);
        }

        if (do_update)
        {
            ++n;
            xmlNodeSetContent(pNode, reinterpret_cast<const xmlChar*>(zNew_value));

            // From the XML sample.
            /*
             * All the elements returned by an XPath query are pointers to
             * elements from the tree *except* namespace nodes where the XPath
             * semantic is different from the implementation in libxml2 tree.
             * As a result when a returned node set is freed when
             * xmlXPathFreeObject() is called, that routine must check the
             * element type. But node from the returned set may have been removed
             * by xmlNodeSetContent() resulting in access to freed data.
             * This can be exercised by running
             *       valgrind xpath2 test3.xml '//discarded' discarded
             * There is 2 ways around it:
             *   - make a copy of the pointers to the nodes from the result set
             *     then call xmlXPathFreeObject() and then modify the nodes
             * or
             *   - remove the reference to the modified nodes from the node set
             *     as they are processed, if they are not namespace nodes.
             */
            if (pNode->type != XML_NAMESPACE_DECL)
            {
                pNodes->nodeTab[i] = NULL;
            }
        }
    }

    return n;
}

int xml_update(xmlNode& node,
               xmlXPathContext& xpath_context,
               const char* zXpath,
               const char* zNew_value,
               const char* zIf_value,
               UpdateWhen update_when)
{
    int n = -1;

    string path(zXpath);
    path = "./" + path;

    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&node,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     &xpath_context);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_update(pXpath_object->nodesetval, zNew_value, zIf_value, update_when);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int mxb::xml::update_if(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(node, *pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int mxb::xml::update_if_not(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(node, *pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF_NOT);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

bool mxb::xml::upsert(xmlNode& node, const char* zPath, const char* zValue, XmlLocation location)
{
    bool rv = true;

    xmlNode* pNode = find_descendant(node, zPath);

    if (pNode)
    {
        xmlNodeSetContent(pNode, reinterpret_cast<const xmlChar*>(zValue));
    }
    else
    {
        rv = insert(node, zPath, zValue, location);
    }

    return rv;
}

namespace
{

int xml_remove(xmlNodeSet* pNodes)
{
    int nNodes = pNodes ? pNodes->nodeNr : 0;

    // From the XML sample.
    /*
     * NOTE: the nodes are processed in reverse order, i.e. reverse document
     *       order because xmlNodeSetContent can actually free up descendant
     *       of the node and such nodes may have been selected too ! Handling
     *       in reverse order ensure that descendant are accessed first, before
     *       they get removed. Mixing XPath and modifications on a tree must be
     *       done carefully !
     */

    for (int i = nNodes - 1; i >= 0; --i)
    {
        const char* zValue = nullptr;
        auto* pNode = pNodes->nodeTab[i];

        if (pNode->type != XML_NAMESPACE_DECL)
        {
            pNodes->nodeTab[i] = NULL;
        }

        if (pNode->prev
            && pNode->prev->type == XML_TEXT_NODE
            && strcmp(reinterpret_cast<char*>(xmlNodeGetContent(pNode->prev)), "\n\t") == 0)
        {
            auto pPrev = pNode->prev;
            xmlUnlinkNode(pPrev);
            xmlFreeNode(pPrev);
        }

        xmlUnlinkNode(pNode);
        xmlFreeNode(pNode);
    }

    return nNodes;
}

int xml_remove(xmlNode& node, xmlXPathContext& xpath_context, const char* zXpath)
{
    int n = -1;

    string path(zXpath);
    path = "./" + path;
    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&node,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     &xpath_context);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_remove(pXpath_object->nodesetval);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}
}

int mxb::xml::remove(xmlNode& node, const char* zXpath)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_remove(node, *pXpath_context, zXpath);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

string mxb::xml::dump(const xmlDoc& doc)
{
    xmlBuffer* pBuffer = xmlBufferCreate();
    xmlDoc* pDoc = const_cast<xmlDoc*>(&doc);
    xmlNodeDump(pBuffer, pDoc, xmlDocGetRootElement(pDoc), 0, 0);
    xmlChar* pXml = xmlBufferDetach(pBuffer);
    const char* zXml = reinterpret_cast<const char*>(pXml);

    string xml(zXml);

    MXS_FREE(pXml);
    xmlBufferFree(pBuffer);

    return xml;
}

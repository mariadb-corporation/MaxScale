/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <algorithm>

namespace maxbase
{

/**
 * This template can be used for providing an STL compatible iterator
 * for an intrusive singly linked list, that is, a list where the
 * elements themselves contain a @c next pointer.
 */
template<class T>
class intrusive_slist_iterator : public std::iterator<std::input_iterator_tag, T>
{
public:
    explicit intrusive_slist_iterator(T& t)
        : m_pT(&t)
    {
    }

    explicit intrusive_slist_iterator()
        : m_pT(nullptr)
    {
    }

    intrusive_slist_iterator& operator++()
    {
        mxb_assert(m_pT);
        m_pT = m_pT->next;
        return *this;
    }

    intrusive_slist_iterator& operator++(int)
    {
        intrusive_slist_iterator prev(*this);
        ++(*this);
        return prev;
    }

    bool operator == (const intrusive_slist_iterator& rhs) const
    {
        return m_pT == rhs.m_pT;
    }

    bool operator != (const intrusive_slist_iterator& rhs) const
    {
        return !(m_pT == rhs.m_pT);
    }

    T& operator * () const
    {
        mxb_assert(m_pT);
        return *m_pT;
    }

private:
    T* m_pT;
};

}

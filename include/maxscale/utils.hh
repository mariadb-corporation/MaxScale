#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <stdio.h>

namespace maxscale
{

/**
 * @class CloserTraits utils.hh <maxscale/utils.hh>
 *
 * A traits class used by Closer. To be specialized for all types that are
 * used with Closer.
 */
template<class T>
struct CloserTraits
{
    /**
     * Closes/frees/destroys a resource.
     *
     * @param t  Close the resource *if* it has not been closed already.
     */
    static void close_if(T t);

    /**
     * Resets a reference to a resource. After the call, the value of t should
     * be such that @c close_if can recognize that the reference already has
     * been closed.
     *
     * @param t  Reference to a resource.
     */
    static void reset(T& t);
};

/**
 * @class Closer utils.hh <maxscale/utils.hh>
 *
 * The template class Closer is a class that is intended to be used
 * for ensuring that a C style resource is released at the end of a
 * scoped block, irrespective of how that block is exited (by reaching
 * the end of it, or by a return or exception in the middle of it).
 *
 * Closer performs the actual resource releasing using CloserTraits
 * that need to be specialized for every type of resource to be managed.
 *
 * Example:
 * @code
 * void f()
 * {
 *     FILE* pFile = fopen(...);
 *
 *     if (pFile)
 *     {
 *         Closer<FILE*> file(pFile);
 *
 *         // Use pFile, call functions that potentually may throw
 *     }
 * }
 * @endcode
 *
 * Without @c Closer all code would have to be placed within try/catch
 * blocks, which quickly becomes unwieldy as the number of managed
 * resources grows.
 */
template<class T>
class Closer
{
public:
    /**
     * Creates the closer and stores the provided resourece. Note that
     * the constructor assumes that the resource exists already.
     *
     * @param resource  The resource whose closing is to be ensured.
     */
    Closer(T resource)
        : m_resource(resource)
    {
    }

    /**
     * Destroys the closer and releases the resource.
     */
    ~Closer()
    {
        CloserTraits<T>::close_if(m_resource);
    }

    /**
     * Returns the original resource. Note that the ownership of the
     * resource remains with the closer.
     *
     * @return The resource that was provided in the constructor.
     */
    T get() const
    {
        return m_resource;
    }

    /**
     * Resets the closer, that is, releases the resource.
     */
    void reset()
    {
        CloserTraits<T>::close_if(m_resource);
        CloserTraits<T>::reset(m_resource);
    }

    /**
     * Resets the closer, that is, releases the resource and assigns a
     * new resource to it.
     */
    void reset(T resource)
    {
        CloserTraits<T>::close_if(m_resource);
        m_resource = resource;
    }

    /**
     * Returns the original resource together with its ownership. That is,
     * after this call the responsibility for releasing the resource belongs
     * to the caller.
     *
     * @return The resource that was provided in the constructor.
     */
    T release()
    {
        T resource = m_resource;
        CloserTraits<T>::reset(m_resource);
        return resource;
    }

private:
    Closer(const Closer&);
    Closer& operator = (const Closer&);

private:
    T m_resource;
};

}


namespace maxscale
{

/**
 * @class CloserTraits<FILE*> utils.hh <maxscale/utils.hh>
 *
 * Specialization of @c CloserTraits for @c FILE*.
 */
template<>
struct CloserTraits<FILE*>
{
    static void close_if(FILE* pFile)
    {
        if (pFile)
        {
            fclose(pFile);
        }
    }

    static void reset(FILE*& pFile)
    {
        pFile = NULL;
    }
};

}

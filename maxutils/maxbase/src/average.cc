/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/average.hh>
#include <iostream>

namespace maxbase
{

//
// CumulativeAverage
//
void CumulativeAverage::add(double ave, long num_samples)
{
    m_num_samples += num_samples;

    if (m_num_samples == num_samples)
    {
        m_ave = ave;
    }
    else
    {
        m_ave = (m_ave * (m_num_samples - num_samples)
                 + ave * num_samples) / m_num_samples;
    }
}

double CumulativeAverage::average() const
{
    return m_ave;
}

long CumulativeAverage::num_samples() const
{
    return m_num_samples;
}

CumulativeAverage& CumulativeAverage::operator+=(const CumulativeAverage& rhs)
{
    this->add(rhs.m_ave, rhs.m_num_samples);
    return *this;
}

CumulativeAverage operator+(const CumulativeAverage& lhs, const CumulativeAverage& rhs)
{
    return CumulativeAverage(lhs) += rhs;
}

void CumulativeAverage::reset()
{
    m_ave = 0;
    m_num_samples = 0;
}

//
// EMAverage
//

EMAverage::EMAverage(double min_alpha, double max_alpha, long sample_max)
    : m_min_alpha{min_alpha}
    , m_max_alpha{max_alpha}
    , m_sample_max{sample_max}
{
}

void EMAverage::add(double ave, long num_samples)
{
    // Give more weight to initial samples.
    long sample_max = std::min(m_num_samples ? m_num_samples : 1, m_sample_max);

    double alpha = m_min_alpha + m_max_alpha
        * std::min(double(num_samples) / sample_max, 1.0);

    m_num_samples += num_samples;
    if (m_num_samples == num_samples)
    {
        m_ave = ave;
    }
    else
    {
        m_ave = alpha * ave + (1 - alpha) * m_ave;
    }
}

void EMAverage::add(const CumulativeAverage& ca)
{
    add(ca.average(), ca.num_samples());
}

double EMAverage::average() const
{
    return m_ave;
}

long EMAverage::num_samples() const
{
    return m_num_samples;
}

void EMAverage::set_sample_max(long sample_max)
{
    m_sample_max = sample_max;
}

long EMAverage::sample_max() const
{
    return m_sample_max;
}

void EMAverage::reset()
{
    m_ave = 0;
    m_num_samples = 0;
}

//
// Average, Average1, AverageN
//
Average::~Average()
{
}

bool Average1::add_value(uint8_t value)
{
    set_value(value);

    // Every addition of a value represents a full cycle.
    if (m_pDependant)
    {
        m_pDependant->add_value(value);
    }

    return true;
}

void Average1::update_value(uint8_t value)
{
    set_value(value);

    if (m_pDependant)
    {
        m_pDependant->update_value(value);
    }
}

AverageN::AverageN(size_t n, Average* pDependant)
    : Average(pDependant)
    , m_buffer(n)
    , m_begin(m_buffer.begin())
    , m_end(m_buffer.end())
    , m_i(m_begin)
    , m_sum(0)
    , m_nValues(0)
{
    mxb_assert(n >= 1);
}

bool AverageN::add_value(uint8_t value)
{
    if (m_nValues == m_buffer.size())
    {
        // If as many values that fit has been added, then remove the
        // least recent value from the sum.
        m_sum -= *m_i;
    }
    else
    {
        // Otherwise make a note that a new value is added.
        ++m_nValues;
    }

    *m_i = value;
    m_sum += *m_i;          // Update the sum of all values.

    m_i = next(m_i);

    uint32_t average = m_sum / m_nValues;

    set_value(average);

    if (m_pDependant)
    {
        if (m_i == m_begin)
        {
            // If we have looped around we have performed a full cycle and will
            // add a new value to the dependant average.
            m_pDependant->add_value(average);
        }
        else
        {
            // Otherwise we just update the most recent value.
            m_pDependant->update_value(average);
        }
    }

    return m_i == m_begin;
}

void AverageN::update_value(uint8_t value)
{
    if (m_nValues == 0)
    {
        // If no values have been added yet, there's nothing to update but we
        // need to add the value.
        add_value(value);
    }
    else
    {
        // Otherwise we update the most recent value.
        auto p = prev(m_i);

        m_sum -= *p;
        *p = value;
        m_sum += *p;

        uint32_t average = m_sum / m_nValues;

        set_value(average);

        if (m_pDependant)
        {
            m_pDependant->update_value(average);
        }
    }
}

AverageN::Data::iterator AverageN::prev(Data::iterator p)
{
    mxb_assert(p >= m_begin);
    mxb_assert(p < m_end);

    if (p > m_begin)
    {
        --p;
    }
    else
    {
        mxb_assert(p == m_begin);
        p = m_end - 1;
    }

    mxb_assert(p >= m_begin);
    mxb_assert(p < m_end);

    return p;
}

AverageN::Data::iterator AverageN::next(Data::iterator p)
{
    mxb_assert(p >= m_begin);
    mxb_assert(p < m_end);

    ++p;

    if (p == m_end)
    {
        p = m_begin;
    }

    mxb_assert(p >= m_begin);
    mxb_assert(p < m_end);

    return p;
}

void AverageN::resize(size_t n)
{
    mxb_assert(n > 0);

    uint32_t nValues = std::min(n, m_nValues);
    Data buffer(nValues);

    if (m_nValues > 0)
    {
        int nSkip = m_nValues - n; // We skip the oldest values.

        if (nSkip < 0)
        {
            nSkip = 0;
        }

        int i = ((m_i - m_begin) + nSkip) % m_nValues;
        auto it = buffer.begin();

        while (nValues)
        {
            *it++ = *(m_begin + i);
            i = (i + 1) % m_nValues;
            --nValues;
        }
    }

    // Now buffer contains the relevant values.

    m_buffer.resize(n);
    m_begin = m_buffer.begin();
    m_end = m_buffer.end();
    m_i = m_begin;
    m_sum = 0;
    m_nValues = 0;

    set_value(0);

    for (auto value : buffer)
    {
        add_value(value);
    }
}

}   // maxbase

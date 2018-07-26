/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxbase/average.hh>
#include <iostream>

namespace maxbase
{

void CumulativeAverage::add(double ave, int num_samples)
{
    m_num_samples += num_samples;

    if (m_num_samples == num_samples)
    {
        m_ave = ave;
    }
    else
    {
        m_ave = (m_ave * (m_num_samples - m_num_last_added)
                 + ave * num_samples) / m_num_samples;
    }
    m_num_last_added = num_samples;
}

double CumulativeAverage::average() const
{
    return m_ave;
}

int CumulativeAverage::num_samples() const
{
    return m_num_samples;
}

CumulativeAverage &CumulativeAverage::operator+=(const CumulativeAverage &rhs)
{
    this->add(rhs.m_ave, rhs.m_num_samples);
    return *this;
}

CumulativeAverage CumulativeAverage::operator+(const CumulativeAverage &rhs) const
{
    return CumulativeAverage(*this) += rhs;
}

void CumulativeAverage::reset()
{
    m_ave = 0;
    m_num_samples = 0;
    m_num_last_added = 0;
}

EMAverage::EMAverage(double min_alpha, double max_alpha, int sample_max) :
    m_min_alpha{min_alpha},
    m_max_alpha{max_alpha},
    m_sample_max{sample_max}
{
}

void EMAverage::add(double ave, int num_samples)
{
    double alpha = m_min_alpha + m_max_alpha *
                   std::min(double(num_samples) / m_sample_max, 1.0);

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

void EMAverage::add(const CumulativeAverage &ca)
{
    add(ca.average(), ca.num_samples());
}

double EMAverage::average() const
{
    return m_ave;
}

int EMAverage::num_samples() const
{
    return m_num_samples;
}

void EMAverage::set_sample_max(int sample_max)
{
    m_sample_max = sample_max;
}

int EMAverage::sample_max() const
{
    return m_sample_max;
}

void EMAverage::reset()
{
    m_ave = 0;
    m_num_samples = 0;
}


} // maxbase

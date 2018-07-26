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
#pragma once

#include <maxbase/ccdefs.hh>

#include <vector>

namespace maxbase
{
/** Regular average, but calculated cumulatively. */
class CumulativeAverage
{
public:
    // add an average made of num_samples
    void   add(double ave, int num_samples = 1);
    double average() const;
    int    num_samples() const;
    void   reset();
    CumulativeAverage& operator+=(const CumulativeAverage& rhs);
    CumulativeAverage operator+(const CumulativeAverage& rhs) const;
private:
    double m_ave = 0;
    int    m_num_samples = 0;
    int    m_num_last_added = 0;
};

/** Exponential Moving Average. */
class EMAverage
{
public:
    EMAverage(double min_alpha, double max_alpha, int sample_max);

    /* add an average made of num_samples
    *  alpha = m_min_alpha + m_max_alpha * std::min(double(num_samples) / sample_max, 1.0);
    *  ave = alpha * ave + (1 - alpha) * sample; */
    void   add(double ave, int num_samples = 1);
    void   add(const CumulativeAverage& ca);
    double average() const;
    int    num_samples() const;
    void   set_sample_max(int sample_max);
    int    sample_max() const;
    void   reset();
private:
    const double  m_min_alpha;
    const double  m_max_alpha;
    int     m_sample_max;
    int     m_num_samples = 0;
    double  m_ave = 0;
};

} // maxbase

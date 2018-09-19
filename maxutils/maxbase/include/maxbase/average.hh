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

/**
 * Generic average calculation helper classes
 */

namespace maxbase
{
/** Regular average, but calculated cumulatively. */
class CumulativeAverage
{
public:
    /**
     * Add an average made of `num_samples`
     *
     * @param ave         The average value
     * @param num_samples How many samples were taken to construct it
     */
    void               add(double ave, int num_samples = 1);

    /**
     * Get the average value
     *
     * @return The average value
     */
    double             average() const;

    /**
     * Get number of samples
     *
     * @return Number of collected samples
     */
    int                num_samples() const;

    /**
     * Reset the average value
     *
     * Sets the average to 0.0 and number of samples to 0.
     */
    void               reset();

    CumulativeAverage& operator+=(const CumulativeAverage& rhs);
private:
    double m_ave = 0;
    int    m_num_samples = 0;
    int    m_num_last_added = 0;
};

CumulativeAverage operator+(const CumulativeAverage& rhs, const CumulativeAverage& lhs);

/**
 * Exponential Moving Average
 *
 */
class EMAverage
{
public:

    /**
     * Construct a new EMAverage
     *
     * @param min_alpha  Base alpha value that is always added
     * @param max_alpha  The extra alpha value
     * @param sample_max Maximum number of samples to use
     */
    EMAverage(double min_alpha, double max_alpha, int sample_max);

    /**
     * Add a new value to the average made of `num_samples` samples
     *
     * Calculates an exponential moving average by applying the following function:
     *
     *     current_ave = alpha * ave + (1 - alpha) * current_ave
     *
     * `current_ave` is the current average value, `ave` is the next value to be added and `alpha` is a value
     * calculated with the following function:
     *
     *     alpha = min_alpha + max_alpha * std::min(double(num_samples) / sample_max, 1.0)
     *
     * `num_samples` is the number of samples `new_val` consists of which defaults to 1. The minimum of
     * `num_samples`and `sample_max()` is used.
     *
     * @param ave         Value to add
     * @param num_samples Number of samples the value consists of
     */
    void   add(double ave, int num_samples = 1);

    /**
     * Add a CumulativeAverage
     *
     * This function is shorthand for `add(ca.average(), ca.num_samples())`.
     *
     * @param ca CumulativeAverage to add
     */
    void   add(const CumulativeAverage& ca);

    /**
     * Get the current average value
     *
     * @return The current average value
     */
    double average() const;

    /**
     * Get number of samples
     *
     * @return The number of samples
     */
    int    num_samples() const;

    /**
     * Set maximum sample size
     *
     * @param sample_max The new sample max size
     */
    void   set_sample_max(int sample_max);

    /**
     * Get maximum sample size
     *
     * @return The maximum sample size
     */
    int    sample_max() const;

    /**
     * Reset the average
     *
     * Sets average value to 0.0 and number of samples to 0.
     */
    void   reset();

private:
    const double m_min_alpha;
    const double m_max_alpha;
    int          m_sample_max;
    int          m_num_samples = 0;
    double       m_ave = 0;
};
}

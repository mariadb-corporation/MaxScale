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
#include <maxbase/atomic.hh>
#include <maxbase/assert.h>

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
    void add(double ave, int num_samples = 1);

    /**
     * Get the average value
     *
     * @return The average value
     */
    double average() const;

    /**
     * Get number of samples
     *
     * @return Number of collected samples
     */
    int num_samples() const;

    /**
     * Reset the average value
     *
     * Sets the average to 0.0 and number of samples to 0.
     */
    void reset();

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
    void add(double ave, int num_samples = 1);

    /**
     * Add a CumulativeAverage
     *
     * This function is shorthand for `add(ca.average(), ca.num_samples())`.
     *
     * @param ca CumulativeAverage to add
     */
    void add(const CumulativeAverage& ca);

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
    int num_samples() const;

    /**
     * Set maximum sample size
     *
     * @param sample_max The new sample max size
     */
    void set_sample_max(int sample_max);

    /**
     * Get maximum sample size
     *
     * @return The maximum sample size
     */
    int sample_max() const;

    /**
     * Reset the average
     *
     * Sets average value to 0.0 and number of samples to 0.
     */
    void reset();

private:
    const double m_min_alpha;
    const double m_max_alpha;
    int          m_sample_max;
    int          m_num_samples = 0;
    double       m_ave = 0;
};

/**
 * Average is a base class for classes intended to be used for calculating
 * averages. An Average may have a dependant Average whose value depends
 * upon the value of the first. At certain moments, an Average may trigger
 * its dependant Average to update itself.
 */
class Average
{
    Average(const Average&) = delete;
    Average& operator=(const Average&) = delete;

public:
    /**
     * Constructor
     *
     * @param pDependant An optional dependant average.
     */
    Average(Average* pDependant = NULL)
        : m_pDependant(pDependant)
        , m_value(0)
    {
    }

    /**
     * Add a value to the Average. The exact meaning depends upon the
     * concrete Average class.
     *
     * If the addition of the value in some sense represents a full cycle
     * in the average calculation, then the instance will call add_value()
     * on its dependant, otherwise it will call update_value(). In both cases
     * with its own value as argument.
     *
     * @param value  The value to be added.
     *
     * @return True if the addition of the value caused a full cycle
     *         in the average calculation, false otherwise.
     */
    virtual bool add_value(uint8_t value) = 0;

    /**
     * Update the value of the Average. The exact meaning depends upon the
     * concrete Average class. Will also call update_value() of its dependant
     * with its own value as argument.
     *
     * @param value  The value to be updated.
     */
    virtual void update_value(uint8_t value) = 0;

    /**
     * Return the average value.
     *
     * @return The value represented by the Average.
     */
    uint8_t value() const
    {
        return mxb::atomic::load(&m_value, mxb::atomic::RELAXED);
    }

protected:
    Average* m_pDependant;  /*< The optional dependant Average. */
    uint32_t m_value;       /*< The current average value. */

protected:
    void set_value(uint32_t value)
    {
        mxb::atomic::store(&m_value, value, mxb::atomic::RELAXED);
    }
};

/**
 * An Average consisting of a single value.
 */
class Average1 : public Average
{
public:
    Average1(Average* pDependant = NULL)
        : Average(pDependant)
    {
    }

    bool add_value(uint8_t value)
    {
        set_value(value);

        // Every addition of a value represents a full cycle.
        if (m_pDependant)
        {
            m_pDependant->add_value(value);
        }

        return true;
    }

    void update_value(uint8_t value)
    {
        set_value(value);

        if (m_pDependant)
        {
            m_pDependant->update_value(value);
        }
    }
};

/**
 * An Average calculated from N values.
 */
template<size_t N>
class AverageN : public Average
{
public:
    AverageN(Average* pDependant = NULL)
        : Average(pDependant)
        , m_end(m_begin + N)
        , m_i(m_begin)
        , m_sum(0)
        , m_nValues(0)
    {
    }

    bool add_value(uint8_t value)
    {
        if (m_nValues == N)
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

    void update_value(uint8_t value)
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
            uint8_t* p = prev(m_i);

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

private:
    uint8_t* prev(uint8_t* p)
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

    uint8_t* next(uint8_t* p)
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

private:
    uint8_t  m_begin[N];    /*< Buffer containing values from which the average is calculated. */
    uint8_t* m_end;         /*< Points to one past the end of the buffer. */
    uint8_t* m_i;           /*< Current position in the buffer. */
    uint32_t m_sum;         /*< Sum of all values in the buffer. */
    uint32_t m_nValues;     /*< How many values the buffer contains. */
};
}

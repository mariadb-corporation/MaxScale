/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
    void add(double ave, long num_samples = 1);

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
    long num_samples() const;

    /**
     * Reset the average value
     *
     * Sets the average to 0.0 and number of samples to 0.
     */
    void reset();

    CumulativeAverage& operator+=(const CumulativeAverage& rhs);
private:
    double m_ave = 0;
    long   m_num_samples = 0;
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
    EMAverage(double min_alpha, double max_alpha, long sample_max);

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
    void add(double ave, long num_samples = 1);

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
    long num_samples() const;

    /**
     * Set maximum sample size
     *
     * @param sample_max The new sample max size
     */
    void set_sample_max(long sample_max);

    /**
     * Get maximum sample size
     *
     * @return The maximum sample size
     */
    long sample_max() const;

    /**
     * Reset the average
     *
     * Sets average value to 0.0 and number of samples to 0.
     */
    void reset();

private:
    const double m_min_alpha;
    const double m_max_alpha;
    long         m_sample_max;
    long         m_num_samples = 0;
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

    virtual ~Average();

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

    bool add_value(uint8_t value) override;
    void update_value(uint8_t value) override;
};

/**
 * An Average calculated from N values.
 */
class AverageN : public Average
{
public:
    using Data = std::vector<uint8_t>;

    AverageN(size_t n, Average* pDependant = nullptr);

    bool add_value(uint8_t value) override;
    void update_value(uint8_t value) override;

    size_t size() const
    {
        return m_buffer.size();
    }

    /**
     * Resize the array of values. If made smaller than originally was,
     * the oldest values are discarded.
     *
     * @param n The new size.
     */
    void resize(size_t n);

private:
    Data::iterator prev(Data::iterator p);
    Data::iterator next(Data::iterator p);

private:
    Data           m_buffer;  /*< Buffer containing values from which the average is calculated. */
    Data::iterator m_begin;   /*< Points to the beginning of the buffer. */
    Data::iterator m_end;     /*< Points to one past the end of the buffer. */
    Data::iterator m_i;       /*< Current position in the buffer. */
    uint32_t       m_sum;     /*< Sum of all values in the buffer. */
    size_t         m_nValues; /*< How many values the buffer contains. */
};
}

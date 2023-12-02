/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>
#include <vector>
#include <fstream>

namespace maxbase
{
class CsvWriter
{
public:

    /**
     * \brief CsvWriter - Simple class to write csv files. The format
     *                    should work in most spread sheet programs
     *                    (tested Numbers, Libreoffice and Excel).
     * \param path      - Path to the file including the extension
     *                    (preferably csv).
     * \param columns   - The csv header
     */
    CsvWriter(const std::string& path, const std::vector<std::string>& columns);

    /**
     * brief add_row
     * param values - The number of values has to match the number
     *                of columns given in the constructor.
     * @return true on success
     *
     * Side effect: the values are csv escaped in place,
     *              and should not be re-used as input.
     */
    bool add_row(std::vector<std::string>& values);

    /**
     * @brief rotate - Re-open the file in append mode.
     *                 If the file has not been moved, rotate
     *                 has no effect.
     * @return true on success.
     */
    bool rotate();

    /**
     * @brief  path
     * @return path to the file
     */
    const std::string path() const;

private:
    std::string              m_path;
    std::vector<std::string> m_columns;
    std::ofstream            m_file;

    /**
     * @brief  open_file - Open the file in append mode
     * @return true on success
     */
    bool open_file();
    /**
     * @brief  write - Write values (header or actual values).
     * @param  values
     * @return true on success
     */
    bool write(std::vector<std::string>& values);

    /**
     * @brief escape - in-place escape, double quotes escaped with double quotes
     * @param str    - modified in-place, if needed
     * @return same instance modified or not
     */
    std::string& inplace_escape(std::string& str);
};
}

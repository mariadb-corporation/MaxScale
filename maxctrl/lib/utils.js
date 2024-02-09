/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

module.exports.strip_colors = function (input) {
  // Based on the regex found in: https://github.com/jonschlinkert/strip-color
  // Try and make sense of this regex to fix the ESLint warning
  // eslint-disable-next-line no-control-regex
  return input.replace(/\x1B\[[(?);]{0,2}(;?\d)*./g, "");
};

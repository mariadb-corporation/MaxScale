/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqloperator.hh"
#include <sstream>

using namespace std;
namespace nosql
{

Operator::~Operator()
{
}

//static
void Operator::unsupported(string_view key)
{
    stringstream ss;
    ss << "Unsupported operator '" << key << "'";

    throw SoftError(ss.str(), error::INTERNAL_ERROR);
}


}

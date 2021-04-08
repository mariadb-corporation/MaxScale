/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "defs.hh"

namespace mxsmongo
{

namespace command
{

class MxsDiagnose final : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        DocumentBuilder doc;

        auto command = value_as<bsoncxx::document::view>();

        int32_t ok = 1;

        if (!command.empty())
        {
            string name = static_cast<string>(command.begin()->key());

            DocumentArguments arguments;
            unique_ptr<Command> sCommand;

            if (m_req.opcode() == Packet::QUERY)
            {
                Query& query = static_cast<Query&>(m_req);

                sCommand = Command::get(&m_database, m_pRequest, query, command, arguments);
            }
            else
            {
                Msg& msg = static_cast<Msg&>(m_req);

                sCommand = Command::get(&m_database, m_pRequest, msg, command, arguments);
            }

            try
            {
                sCommand->diagnose(doc);
            }
            catch (const Exception& x)
            {
                doc.clear();

                DocumentBuilder error;
                x.create_response(*sCommand, error);

                doc.append(kvp("error", error.extract()));
                ok = 0;
            }
            catch (const std::exception& x)
            {
                doc.clear();

                doc.append(kvp("error", x.what()));
                ok = 0;
            }
        }

        doc.append(kvp("ok", ok));

        return create_response(doc.extract());
    }
};

}

}

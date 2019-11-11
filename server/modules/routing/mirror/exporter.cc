/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "exporter.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Exports to maxscale.log on info level
class LogExporter : public Exporter
{
public:
    void ship(json_t* obj) final
    {
        MXS_INFO("%s", mxs::json_dump(obj, JSON_COMPACT).c_str());
    }
};

// Exports to a file
class FileExporter : public Exporter
{
public:
    FileExporter(int fd)
        : m_fd(fd)
    {
    }

    ~FileExporter()
    {
        close(m_fd);
    }

    void ship(json_t* obj) final
    {
        auto str = mxs::json_dump(obj, JSON_COMPACT) + '\n';
        write(m_fd, str.c_str(), str.length());
    }

private:
    int m_fd;
};

std::unique_ptr<Exporter> build_exporter(MXS_CONFIG_PARAMETER* params)
{
    std::unique_ptr<Exporter> rval;
    Exporter::Type type = (Exporter::Type)params->get_enum(CN_EXPORTER, exporter_type_values);
    std::string uri = params->get_string(CN_FILE);

    switch (type)
    {
    case Exporter::Type::LOG:
        rval.reset(new LogExporter);
        break;

    case Exporter::Type::FILE:
        {
            int fd = open(uri.c_str(), O_APPEND | O_WRONLY | O_CREAT,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

            if (fd != -1)
            {
                rval.reset(new FileExporter(fd));
            }
            else
            {
                MXS_ERROR("Failed to open file '%s', %d, %s", uri.c_str(), errno, mxs_strerror(errno));
            }
        }
        break;
    }

    return rval;
}

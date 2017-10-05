#include "maxscales.h"

Maxscales::Maxscales(const char *pref, const char *test_cwd, bool verbose)
{
    strcpy(prefix, pref);
    this->verbose = verbose;
    strcpy(test_dir, test_cwd);
    read_env();
}

int Maxscales::read_env()
{
    char * env;
    char env_name[64];

    read_basic_env();

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            sprintf(env_name, "%s_%03d_cnf", prefix, i);
            env = getenv(env_name);
            if (env == NULL)
            {
                sprintf(env_name, "%s_cnf", prefix);
                env = getenv(env_name);
            }
            if (env != NULL)
            {
                sprintf(maxscale_cnf[i], "%s", env);
            }
            else
            {
                sprintf(maxscale_cnf[i], "/etc/maxscale.cnf");
            }

            sprintf(env_name, "%s_%03d_log_dir", prefix, i);
            env = getenv(env_name);
            if (env == NULL)
            {
                sprintf(env_name, "%s_log_dir", prefix);
                env = getenv(env_name);
            }

            if (env != NULL)
            {
                sprintf(maxscale_log_dir[i], "%s", env);
            }
            else
            {
                sprintf(maxscale_log_dir[i], "/var/log/maxscale/");
            }

            sprintf(env_name, "%s_%03d_binlog_dir", prefix, i);
            env = getenv(env_name);
            if (env == NULL)
            {
                sprintf(env_name, "%s_binlog_dir", prefix);
                env = getenv(env_name);
            }
            if (env != NULL)
            {
                sprintf(maxscale_binlog_dir[i], "%s", env);
            }
            else
            {
                sprintf(maxscale_binlog_dir[i], "/var/lib/maxscale/Binlog_Service/");
            }

            sprintf(env_name, "%s_%03d_maxadmin_password", prefix, i);
            env = getenv(env_name);
            if (env == NULL)
            {
                sprintf(env_name, "%s_maxadmin_password", prefix);
                env = getenv(env_name);
            }
            if (env != NULL)
            {
                sprintf(maxadmin_password[i], "%s", env);
            }
            else
            {
                sprintf(maxadmin_password[i], "mariadb");
            }

            rwsplit_port[i] = 4006;
            readconn_master_port[i] = 4008;
            readconn_slave_port[i] = 4009;
            binlog_port[i] = 5306;

            ports[i][0] = rwsplit_port[i];
            ports[i][1] = readconn_master_port[i];
            ports[i][2] = readconn_slave_port[i];

            N_ports[0] = 3;

        }
    }

    return 0;
}


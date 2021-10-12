/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    schemas: [
        {
            name: 'mysql',
            tables: [
                {
                    name: 'columns_priv',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'Db',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Table_name',
                            dataType: 'char',
                        },
                        {
                            name: 'Column_name',
                            dataType: 'char',
                        },
                        {
                            name: 'Timestamp',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'Column_priv',
                            dataType: 'set',
                        },
                    ],
                },
                {
                    name: 'column_stats',
                    columns: [
                        {
                            name: 'db_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'table_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'column_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'min_value',
                            dataType: 'varbinary',
                        },
                        {
                            name: 'max_value',
                            dataType: 'varbinary',
                        },
                        {
                            name: 'nulls_ratio',
                            dataType: 'decimal',
                        },
                        {
                            name: 'avg_length',
                            dataType: 'decimal',
                        },
                        {
                            name: 'avg_frequency',
                            dataType: 'decimal',
                        },
                        {
                            name: 'hist_size',
                            dataType: 'tinyint',
                        },
                        {
                            name: 'hist_type',
                            dataType: 'enum',
                        },
                        {
                            name: 'histogram',
                            dataType: 'varbinary',
                        },
                    ],
                },
                {
                    name: 'db',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'Db',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Select_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Insert_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Update_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Delete_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Create_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Drop_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Grant_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'References_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Index_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Alter_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Create_tmp_table_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Lock_tables_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Create_view_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Show_view_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Create_routine_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Alter_routine_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Execute_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Event_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Trigger_priv',
                            dataType: 'enum',
                        },
                        {
                            name: 'Delete_history_priv',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'event',
                    columns: [
                        {
                            name: 'db',
                            dataType: 'char',
                        },
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                        {
                            name: 'body',
                            dataType: 'longblob',
                        },
                        {
                            name: 'definer',
                            dataType: 'char',
                        },
                        {
                            name: 'execute_at',
                            dataType: 'datetime',
                        },
                        {
                            name: 'interval_value',
                            dataType: 'int',
                        },
                        {
                            name: 'interval_field',
                            dataType: 'enum',
                        },
                        {
                            name: 'created',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'modified',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'last_executed',
                            dataType: 'datetime',
                        },
                        {
                            name: 'starts',
                            dataType: 'datetime',
                        },
                        {
                            name: 'ends',
                            dataType: 'datetime',
                        },
                        {
                            name: 'status',
                            dataType: 'enum',
                        },
                        {
                            name: 'on_completion',
                            dataType: 'enum',
                        },
                        {
                            name: 'sql_mode',
                            dataType: 'set',
                        },
                        {
                            name: 'comment',
                            dataType: 'char',
                        },
                        {
                            name: 'originator',
                            dataType: 'int',
                        },
                        {
                            name: 'time_zone',
                            dataType: 'char',
                        },
                        {
                            name: 'character_set_client',
                            dataType: 'char',
                        },
                        {
                            name: 'collation_connection',
                            dataType: 'char',
                        },
                        {
                            name: 'db_collation',
                            dataType: 'char',
                        },
                        {
                            name: 'body_utf8',
                            dataType: 'longblob',
                        },
                    ],
                },
                {
                    name: 'func',
                    columns: [
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                        {
                            name: 'ret',
                            dataType: 'tinyint',
                        },
                        {
                            name: 'dl',
                            dataType: 'char',
                        },
                        {
                            name: 'type',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'general_log',
                    columns: [
                        {
                            name: 'event_time',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'user_host',
                            dataType: 'mediumtext',
                        },
                        {
                            name: 'thread_id',
                            dataType: 'bigint',
                        },
                        {
                            name: 'server_id',
                            dataType: 'int',
                        },
                        {
                            name: 'command_type',
                            dataType: 'varchar',
                        },
                        {
                            name: 'argument',
                            dataType: 'mediumtext',
                        },
                    ],
                },
                {
                    name: 'global_priv',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Priv',
                            dataType: 'longtext',
                        },
                    ],
                },
                {
                    name: 'gtid_slave_pos',
                    columns: [
                        {
                            name: 'domain_id',
                            dataType: 'int',
                        },
                        {
                            name: 'sub_id',
                            dataType: 'bigint',
                        },
                        {
                            name: 'server_id',
                            dataType: 'int',
                        },
                        {
                            name: 'seq_no',
                            dataType: 'bigint',
                        },
                    ],
                },
                {
                    name: 'help_category',
                    columns: [
                        {
                            name: 'help_category_id',
                            dataType: 'smallint',
                        },
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                        {
                            name: 'parent_category_id',
                            dataType: 'smallint',
                        },
                        {
                            name: 'url',
                            dataType: 'text',
                        },
                    ],
                },
                {
                    name: 'help_keyword',
                    columns: [
                        {
                            name: 'help_keyword_id',
                            dataType: 'int',
                        },
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                    ],
                },
                {
                    name: 'help_relation',
                    columns: [
                        {
                            name: 'help_topic_id',
                            dataType: 'int',
                        },
                        {
                            name: 'help_keyword_id',
                            dataType: 'int',
                        },
                    ],
                },
                {
                    name: 'help_topic',
                    columns: [
                        {
                            name: 'help_topic_id',
                            dataType: 'int',
                        },
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                        {
                            name: 'help_category_id',
                            dataType: 'smallint',
                        },
                        {
                            name: 'description',
                            dataType: 'text',
                        },
                        {
                            name: 'example',
                            dataType: 'text',
                        },
                        {
                            name: 'url',
                            dataType: 'text',
                        },
                    ],
                },
                {
                    name: 'index_stats',
                    columns: [
                        {
                            name: 'db_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'table_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'index_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'prefix_arity',
                            dataType: 'int',
                        },
                        {
                            name: 'avg_frequency',
                            dataType: 'decimal',
                        },
                    ],
                },
                {
                    name: 'innodb_index_stats',
                    columns: [
                        {
                            name: 'database_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'table_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'index_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'last_update',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'stat_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'stat_value',
                            dataType: 'bigint',
                        },
                        {
                            name: 'sample_size',
                            dataType: 'bigint',
                        },
                        {
                            name: 'stat_description',
                            dataType: 'varchar',
                        },
                    ],
                },
                {
                    name: 'innodb_table_stats',
                    columns: [
                        {
                            name: 'database_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'table_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'last_update',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'n_rows',
                            dataType: 'bigint',
                        },
                        {
                            name: 'clustered_index_size',
                            dataType: 'bigint',
                        },
                        {
                            name: 'sum_of_other_index_sizes',
                            dataType: 'bigint',
                        },
                    ],
                },
                {
                    name: 'plugin',
                    columns: [
                        {
                            name: 'name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'dl',
                            dataType: 'varchar',
                        },
                    ],
                },
                {
                    name: 'proc',
                    columns: [
                        {
                            name: 'db',
                            dataType: 'char',
                        },
                        {
                            name: 'name',
                            dataType: 'char',
                        },
                        {
                            name: 'type',
                            dataType: 'enum',
                        },
                        {
                            name: 'specific_name',
                            dataType: 'char',
                        },
                        {
                            name: 'language',
                            dataType: 'enum',
                        },
                        {
                            name: 'sql_data_access',
                            dataType: 'enum',
                        },
                        {
                            name: 'is_deterministic',
                            dataType: 'enum',
                        },
                        {
                            name: 'security_type',
                            dataType: 'enum',
                        },
                        {
                            name: 'param_list',
                            dataType: 'blob',
                        },
                        {
                            name: 'returns',
                            dataType: 'longblob',
                        },
                        {
                            name: 'body',
                            dataType: 'longblob',
                        },
                        {
                            name: 'definer',
                            dataType: 'char',
                        },
                        {
                            name: 'created',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'modified',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'sql_mode',
                            dataType: 'set',
                        },
                        {
                            name: 'comment',
                            dataType: 'text',
                        },
                        {
                            name: 'character_set_client',
                            dataType: 'char',
                        },
                        {
                            name: 'collation_connection',
                            dataType: 'char',
                        },
                        {
                            name: 'db_collation',
                            dataType: 'char',
                        },
                        {
                            name: 'body_utf8',
                            dataType: 'longblob',
                        },
                        {
                            name: 'aggregate',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'procs_priv',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'Db',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Routine_name',
                            dataType: 'char',
                        },
                        {
                            name: 'Routine_type',
                            dataType: 'enum',
                        },
                        {
                            name: 'Grantor',
                            dataType: 'char',
                        },
                        {
                            name: 'Proc_priv',
                            dataType: 'set',
                        },
                        {
                            name: 'Timestamp',
                            dataType: 'timestamp',
                        },
                    ],
                },
                {
                    name: 'proxies_priv',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Proxied_host',
                            dataType: 'char',
                        },
                        {
                            name: 'Proxied_user',
                            dataType: 'char',
                        },
                        {
                            name: 'With_grant',
                            dataType: 'tinyint',
                        },
                        {
                            name: 'Grantor',
                            dataType: 'char',
                        },
                        {
                            name: 'Timestamp',
                            dataType: 'timestamp',
                        },
                    ],
                },
                {
                    name: 'roles_mapping',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Role',
                            dataType: 'char',
                        },
                        {
                            name: 'Admin_option',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'servers',
                    columns: [
                        {
                            name: 'Server_name',
                            dataType: 'char',
                        },
                        {
                            name: 'Host',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Db',
                            dataType: 'char',
                        },
                        {
                            name: 'Username',
                            dataType: 'char',
                        },
                        {
                            name: 'Password',
                            dataType: 'char',
                        },
                        {
                            name: 'Port',
                            dataType: 'int',
                        },
                        {
                            name: 'Socket',
                            dataType: 'char',
                        },
                        {
                            name: 'Wrapper',
                            dataType: 'char',
                        },
                        {
                            name: 'Owner',
                            dataType: 'varchar',
                        },
                    ],
                },
                {
                    name: 'slow_log',
                    columns: [
                        {
                            name: 'start_time',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'user_host',
                            dataType: 'mediumtext',
                        },
                        {
                            name: 'query_time',
                            dataType: 'time',
                        },
                        {
                            name: 'lock_time',
                            dataType: 'time',
                        },
                        {
                            name: 'rows_sent',
                            dataType: 'int',
                        },
                        {
                            name: 'rows_examined',
                            dataType: 'int',
                        },
                        {
                            name: 'db',
                            dataType: 'varchar',
                        },
                        {
                            name: 'last_insert_id',
                            dataType: 'int',
                        },
                        {
                            name: 'insert_id',
                            dataType: 'int',
                        },
                        {
                            name: 'server_id',
                            dataType: 'int',
                        },
                        {
                            name: 'sql_text',
                            dataType: 'mediumtext',
                        },
                        {
                            name: 'thread_id',
                            dataType: 'bigint',
                        },
                        {
                            name: 'rows_affected',
                            dataType: 'int',
                        },
                    ],
                },
                {
                    name: 'tables_priv',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'Db',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Table_name',
                            dataType: 'char',
                        },
                        {
                            name: 'Grantor',
                            dataType: 'char',
                        },
                        {
                            name: 'Timestamp',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'Table_priv',
                            dataType: 'set',
                        },
                        {
                            name: 'Column_priv',
                            dataType: 'set',
                        },
                    ],
                },
                {
                    name: 'table_stats',
                    columns: [
                        {
                            name: 'db_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'table_name',
                            dataType: 'varchar',
                        },
                        {
                            name: 'cardinality',
                            dataType: 'bigint',
                        },
                    ],
                },
                {
                    name: 'time_zone',
                    columns: [
                        {
                            name: 'Time_zone_id',
                            dataType: 'int',
                        },
                        {
                            name: 'Use_leap_seconds',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'time_zone_leap_second',
                    columns: [
                        {
                            name: 'Transition_time',
                            dataType: 'bigint',
                        },
                        {
                            name: 'Correction',
                            dataType: 'int',
                        },
                    ],
                },
                {
                    name: 'time_zone_name',
                    columns: [
                        {
                            name: 'Name',
                            dataType: 'char',
                        },
                        {
                            name: 'Time_zone_id',
                            dataType: 'int',
                        },
                    ],
                },
                {
                    name: 'time_zone_transition',
                    columns: [
                        {
                            name: 'Time_zone_id',
                            dataType: 'int',
                        },
                        {
                            name: 'Transition_time',
                            dataType: 'bigint',
                        },
                        {
                            name: 'Transition_type_id',
                            dataType: 'int',
                        },
                    ],
                },
                {
                    name: 'time_zone_transition_type',
                    columns: [
                        {
                            name: 'Time_zone_id',
                            dataType: 'int',
                        },
                        {
                            name: 'Transition_type_id',
                            dataType: 'int',
                        },
                        {
                            name: 'Offset',
                            dataType: 'int',
                        },
                        {
                            name: 'Is_DST',
                            dataType: 'tinyint',
                        },
                        {
                            name: 'Abbreviation',
                            dataType: 'char',
                        },
                    ],
                },
                {
                    name: 'transaction_registry',
                    columns: [
                        {
                            name: 'transaction_id',
                            dataType: 'bigint',
                        },
                        {
                            name: 'commit_id',
                            dataType: 'bigint',
                        },
                        {
                            name: 'begin_timestamp',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'commit_timestamp',
                            dataType: 'timestamp',
                        },
                        {
                            name: 'isolation_level',
                            dataType: 'enum',
                        },
                    ],
                },
                {
                    name: 'user',
                    columns: [
                        {
                            name: 'Host',
                            dataType: 'char',
                        },
                        {
                            name: 'User',
                            dataType: 'char',
                        },
                        {
                            name: 'Password',
                            dataType: 'longtext',
                        },
                        {
                            name: 'Select_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Insert_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Update_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Delete_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Drop_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Reload_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Shutdown_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Process_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'File_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Grant_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'References_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Index_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Alter_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Show_db_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Super_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_tmp_table_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Lock_tables_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Execute_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Repl_slave_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Repl_client_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_view_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Show_view_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_routine_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Alter_routine_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_user_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Event_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Trigger_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Create_tablespace_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'Delete_history_priv',
                            dataType: 'varchar',
                        },
                        {
                            name: 'ssl_type',
                            dataType: 'varchar',
                        },
                        {
                            name: 'ssl_cipher',
                            dataType: 'longtext',
                        },
                        {
                            name: 'x509_issuer',
                            dataType: 'longtext',
                        },
                        {
                            name: 'x509_subject',
                            dataType: 'longtext',
                        },
                        {
                            name: 'max_questions',
                            dataType: 'bigint',
                        },
                        {
                            name: 'max_updates',
                            dataType: 'bigint',
                        },
                        {
                            name: 'max_connections',
                            dataType: 'bigint',
                        },
                        {
                            name: 'max_user_connections',
                            dataType: 'bigint',
                        },
                        {
                            name: 'plugin',
                            dataType: 'longtext',
                        },
                        {
                            name: 'authentication_string',
                            dataType: 'longtext',
                        },
                        {
                            name: 'password_expired',
                            dataType: 'varchar',
                        },
                        {
                            name: 'is_role',
                            dataType: 'varchar',
                        },
                        {
                            name: 'default_role',
                            dataType: 'longtext',
                        },
                        {
                            name: 'max_statement_time',
                            dataType: 'decimal',
                        },
                    ],
                },
            ],
        },
    ],
}

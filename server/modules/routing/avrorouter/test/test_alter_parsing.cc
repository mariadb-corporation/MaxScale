#include "../avro_schema.cc"

static struct
{
    const char* statement;
    const char* target;
    bool        rval;
} data[] =
{
    {"/*!40000 ALTER TABLE `t1` DISABLE KEYS */", NULL, false},
    {"/*!40000 ALTER TABLE `t1` ENABLE KEYS */", NULL, false},
    {"ADD COLUMN `a` INT", NULL, false},
    {"ADD COLUMN `a`", NULL, false},
    {"ALTER TABLE `t1` ADD `account_id` INT", NULL, false},
    {"ALTER TABLE `t1` ADD `amount` INT", NULL, false},
    {"ALTER TABLE `t1` ADD `app_id` VARCHAR(64)", NULL, false},
    {"ALTER TABLE `t1` ADD `create_time` DATETIME", NULL, false},
    {"alter TABLE t1 add `end_time` varchar(10) DEFAULT NULL COMMENT 'this is a comment'", NULL, false},
    {"ALTER TABLE `t1` ADD `expire_time` DATETIME", NULL, false},
    {"ALTER TABLE `t1` ADD `id_a` VARCHAR(128)", NULL, false},
    {"ALTER TABLE `t1` ADD `id` BIGINT(20)", NULL, false},
    {"ALTER TABLE `t1` ADD `id` VARCHAR(64)", NULL, false},
    {"ALTER TABLE `t1` ADD `node_state` INT(4)", NULL, false},
    {"ALTER TABLE `t1` ADD `no` INT", NULL, false},
    {"ALTER TABLE `t1` ADD `order_id` INT", NULL, false},
    {"alter TABLE t1 add `start_time` varchar(10) DEFAULT NULL COMMENT 'this is a comment'", NULL, false},
    {"ALTER TABLE `t1` ADD `status` INT", NULL, false},
    {"ALTER TABLE `t1` ADD `task_id` BIGINT(20)", NULL, false},
    {"alter TABLE t1 add `undo` int(1) DEFAULT '0' COMMENT 'this is a comment'", NULL, false},
    {"alter table `t1` add unique (`a`,`id`)", NULL, false},
    {"alter table `t1` add unique (`a`)", NULL, false},
    {"alter table `t1` add UNIQUE(`a`)", NULL, false},
    {"ALTER TABLE `t1` ADD UNIQUE `idx_id` USING BTREE (`id`, `result`)", NULL, false},
    {"ALTER TABLE `t1` ADD `update_time` INT", NULL, false},
    {"ALTER TABLE `t1` ADD `username` VARCHAR(16)", NULL, false},
    {"ALTER TABLE `t1` AUTO_INCREMENT = 1", NULL, false},
    {"ALTER TABLE `t1` CHANGE `account_id` `account_id` BIGINT(20)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `amount` `amount` DECIMAL(32,2)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `app_id` `app_id` VARCHAR(64)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `business_id` `business_id` VARCHAR(128)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `business_id` `business_id` VARCHAR(64)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `business_unique_no` `business_unique_no` VARCHAR(64)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `expire_time` `expire_time` DATETIME", NULL, false},
    {"ALTER TABLE `t1` CHANGE `id_a` `id_a` VARCHAR(128)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `id` `id` BIGINT(20)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `node_state` `node_state` INT(4)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `order_id` `order_id` BIGINT(20)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `status` `status` INT(1)", NULL, false},
    {"ALTER TABLE `t1` CHANGE `update_time` `update_time` TIMESTAMP", NULL, false},
    {"ALTER TABLE `t1` CHANGE `username` `username` VARCHAR(16)", NULL, false},
    {"ALTER TABLE `t1` COMMENT = 'a comment'", NULL, false},
    {"alter table `t1` drop index a", NULL, false},
    {"alter table t1 drop index t1_idx", NULL, false},
    {"alter table t1 index(account_id, business_id)", NULL, false},
    {"ALTER TABLE `t1` MODIFY COLUMN `expire_time` DATETIME DEFAULT NULL COMMENT 'this is a comment' AFTER `update_time`", "update_time", true},
    {"ALTER TABLE `t1` MODIFY COLUMN `id_a` VARCHAR(128) CHARACTER SET utf8 COLLATE utf8_general_ci COMMENT 'this is a comment' AFTER `username`", "username", true},
    {"ALTER TABLE `t1` MODIFY COLUMN `number` VARCHAR(64) CHARACTER SET utf8 COLLATE utf8_general_ci DEFAULT NULL COMMENT 'this is a comment' AFTER `business_id`", "business_id", true},
    {"ALTER TABLE `t1` MODIFY COLUMN `task_id` BIGINT(20) DEFAULT NULL COMMENT 'this is a comment' AFTER `business_id`", "business_id", true},
    {"ALTER TABLE `t1` MODIFY COLUMN `username` VARCHAR(16) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL COMMENT 'this is a comment' AFTER `business_id`", "business_id", true},
    {"ALTER TABLE `t1` RENAME `t2`", NULL, false},
    {"ALTER TABLE `db1`.`t1` ADD COLUMN `num` varchar(32) COMMENT 'this is a comment' AFTER `bank_name`", "bank_name", true},
    {"ALTER TABLE `db1`.`t1` ADD INDEX `idx_node_state` USING BTREE (`node_state`) comment ''", NULL, false},
    {"ALTER TABLE `db1`.`t1` CHANGE COLUMN `num` `code` varchar(32) DEFAULT NULL COMMENT 'this is a comment'", NULL, false},
    {"ALTER TABLE `db1`.`t1` DROP INDEX `a`, ADD INDEX `a` USING BTREE (`a`) comment ''", NULL, false},
    {"ALTER TABLE `db1`.`t1` DROP INDEX `a`, ADD INDEX `idx_a` USING BTREE (`a`) comment ''", NULL, false},
    {"ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT AFTER `b`", "b", true},
    {"ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT first", NULL, true},
    {"ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT", NULL, false},
    {"ALTER TABLE `t1` MODIFY COLUMN `a` INT PRIMARY KEY", NULL, false},
    {NULL}
};

int main(int argc, char** argv)
{
    int rval = 0;

    for (int i = 0; data[i].statement; i++)
    {
        const char* target = NULL;
        int len = 0;
        const char* stmt = data[i].statement;
        const char* end = data[i].statement + strlen(data[i].statement);

        if (get_placement_specifier(stmt, end, &target, &len) != data[i].rval)
        {
            const char* a = data[i].rval ? "true" : "false";
            const char* b = data[i].rval ? "false" : "true";
            printf("Expected '%s', got '%s' for '%s'\n", a, b, data[i].statement);
            rval++;
        }
        else if (((bool)data[i].target != (bool)target) || (strncmp(target, data[i].target, len) != 0))
        {
            printf("Expected '%s', got '%.*s' for '%s'\n", data[i].target, len, target, data[i].statement);
            rval++;
        }
    }

    return rval;
}

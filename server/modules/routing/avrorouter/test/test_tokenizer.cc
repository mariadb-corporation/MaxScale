#include "../../replicator/tokenizer.hh"

#include <iostream>

using T = tok::Type;

static struct
{
    const char* statement;
    std::vector<tok::Tokenizer::Token> expected;
} data[] = {{"ADD COLUMN `a` INT", {T::ADD, T::COLUMN, T::ID, T::ID}},
    {"ADD COLUMN `a`", {T::ADD, T::COLUMN, T::ID}},
    {"ALTER TABLE `t1` ADD `account_id` INT", {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID}},
    {"ALTER  ONLINE TABLE `t1` ADD COLUMN a INT",
        {T::ALTER, T::ONLINE, T::TABLE, T::ID, T::ADD, T::COLUMN, T::ID, T::ID}},
    {"ALTER  IGNORE TABLE `t1` ADD COLUMN a INT",
        {T::ALTER, T::IGNORE, T::TABLE, T::ID, T::ADD, T::COLUMN, T::ID, T::ID}},
    {"ALTER TABLE `t1` ADD `amount` INT", {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID}},
    {"ALTER TABLE `t1` ADD `amount` INT NULL", {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID, T::SQLNULL}},
    {"ALTER TABLE `t1` ADD `amount` INT NOT NULL",
        {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID, T::NOT, T::SQLNULL}},
    {"ALTER TABLE `t1` ADD `app_id` VARCHAR(64)",
        {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID, T::LP, T::ID, T::RP}},
    {"ALTER TABLE `t1` ADD `create_time` DATETIME", {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID}},
    {"alter TABLE t1 add `end_time` varchar(10) DEFAULT NULL COMMENT 'this is a comment'",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::ADD,
            T::ID,
            T::ID,
            T::LP,
            T::ID,
            T::RP,
            T::DEFAULT,
            T::SQLNULL,
            T::COMMENT,
            T::ID}},
    {"ALTER TABLE `t1` ADD `id_a` VARCHAR(128)",
        {T::ALTER, T::TABLE, T::ID, T::ADD, T::ID, T::ID, T::LP, T::ID, T::RP}},
    {"alter TABLE t1 add `undo` int(1) DEFAULT '0' COMMENT 'this is a comment'",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::ADD,
            T::ID,
            T::ID,
            T::LP,
            T::ID,
            T::RP,
            T::DEFAULT,
            T::ID,
            T::COMMENT,
            T::ID}},
    {"alter table `t1` add unique (`a`,`id`)",
        {T::ALTER, T::TABLE, T::ID, T::ADD, T::UNIQUE, T::LP, T::ID, T::COMMA, T::ID, T::RP}},
    {"alter table `t1` add unique (`a`)",
        {T::ALTER, T::TABLE, T::ID, T::ADD, T::UNIQUE, T::LP, T::ID, T::RP}},
    {"alter table `t1` add UNIQUE(`a`)", {T::ALTER, T::TABLE, T::ID, T::ADD, T::UNIQUE, T::LP, T::ID, T::RP}},
    {"ALTER TABLE `t1` ADD UNIQUE `idx_id` USING BTREE (`id`, `result`)",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::ADD,
            T::UNIQUE,
            T::ID,
            T::ID,
            T::ID,
            T::LP,
            T::ID,
            T::COMMA,
            T::ID,
            T::RP}},
    {"ALTER TABLE `t1` AUTO_INCREMENT = 1", {T::ALTER, T::TABLE, T::ID, T::AUTO_INCREMENT, T::EQ, T::ID}},
    {"ALTER TABLE `t1` CHANGE `account_id` `account_id` BIGINT(20)",
        {T::ALTER, T::TABLE, T::ID, T::CHANGE, T::ID, T::ID, T::ID, T::LP, T::ID, T::RP}},
    {"ALTER TABLE `t1` CHANGE `amount` `amount` DECIMAL(32,2)",
        {T::ALTER, T::TABLE, T::ID, T::CHANGE, T::ID, T::ID, T::ID, T::LP, T::ID, T::COMMA, T::ID, T::RP}},
    {"ALTER TABLE `t1` COMMENT = 'a comment'", {T::ALTER, T::TABLE, T::ID, T::COMMENT, T::EQ, T::ID}},
    {"alter table `t1` drop index a", {T::ALTER, T::TABLE, T::ID, T::DROP, T::INDEX, T::ID}},
    {"alter table t1 drop index t1_idx", {T::ALTER, T::TABLE, T::ID, T::DROP, T::INDEX, T::ID}},
    {"alter table t1 index(account_id, business_id)",
        {T::ALTER, T::TABLE, T::ID, T::INDEX, T::LP, T::ID, T::COMMA, T::ID, T::RP}},
    {"ALTER TABLE `t1` MODIFY COLUMN `expire_time` DATETIME DEFAULT NULL COMMENT 'this is a comment' AFTER "
     "`update_time`",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::MODIFY,
            T::COLUMN,
            T::ID,
            T::ID,
            T::DEFAULT,
            T::SQLNULL,
            T::COMMENT,
            T::ID,
            T::AFTER,
            T::ID}},
    {"ALTER TABLE `db1`.`t1` CHANGE COLUMN `num` `code` varchar(32) DEFAULT NULL COMMENT 'this is a comment'",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::DOT,
            T::ID,
            T::CHANGE,
            T::COLUMN,
            T::ID,
            T::ID,
            T::ID,
            T::LP,
            T::ID,
            T::RP,
            T::DEFAULT,
            T::SQLNULL,
            T::COMMENT,
            T::ID}},
    {"ALTER TABLE `db1`.`t1` DROP INDEX `a`, ADD INDEX `idx_a` USING BTREE (`a`) comment ''",
        {T::ALTER,
            T::TABLE,
            T::ID,
            T::DOT,
            T::ID,
            T::DROP,
            T::INDEX,
            T::ID,
            T::COMMA,
            T::ADD,
            T::INDEX,
            T::ID,
            T::ID,
            T::ID,
            T::LP,
            T::ID,
            T::RP,
            T::COMMENT,
            T::ID}},
    {
        "ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT AFTER `b`",
        {T::ALTER, T::TABLE, T::ID, T::CHANGE, T::COLUMN, T::ID, T::ID, T::ID, T::AFTER, T::ID},
    },
    {"ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT first",
        {T::ALTER, T::TABLE, T::ID, T::CHANGE, T::COLUMN, T::ID, T::ID, T::ID, T::FIRST}},
    {"ALTER TABLE `t1` CHANGE COLUMN `a` `c` INT",
        {T::ALTER, T::TABLE, T::ID, T::CHANGE, T::COLUMN, T::ID, T::ID, T::ID}},
    {"ALTER TABLE `t1` MODIFY COLUMN `a` INT PRIMARY KEY",
        {T::ALTER, T::TABLE, T::ID, T::MODIFY, T::COLUMN, T::ID, T::ID, T::PRIMARY, T::KEY}},
    {"ALTER TABLE `t1` RENAME `t2`", {T::ALTER, T::TABLE, T::ID, T::RENAME, T::ID}},
    {"RENAME TABLE `t1` TO `t1_old`, `t2` TO `t1`",
        {T::RENAME, T::TABLE, T::ID, T::TO, T::ID, T::COMMA, T::ID, T::TO, T::ID}},
    {NULL}};

int main(int argc, char** argv)
{
    int rval = 0;

    for (int i = 0; data[i].statement; i++)
    {
        auto tk = tok::Tokenizer::tokenize(data[i].statement);
        std::vector<tok::Tokenizer::Token> res(tk.begin(), tk.end());

        if (res != data[i].expected)
        {
            std::cout << "SQL: " << data[i].statement << std::endl;

            std::cout << "Expected:" << std::endl;
            for (const auto& a : data[i].expected)
            {
                std::cout << a.to_string() << " ";
            }
            std::cout << std::endl;

            std::cout << "Actual:" << std::endl;
            for (const auto& a : res)
            {
                std::cout << a.to_string() << " ";
            }
            std::cout << '\n' << std::endl;

            rval++;
        }
    }

    return rval;
}

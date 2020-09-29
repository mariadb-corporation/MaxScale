/*
 * Copied from http://mongocxx.org/mongocxx-v3/installation
 *
 * Presumably the license is Apache 2.
 */

#include <iostream>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>

int main(int argc, char** argv) {
    mongocxx::uri uri {};

    if (argc > 1)
    {
        uri = mongocxx::uri { argv[argc - 1] };
    }

    mongocxx::instance inst{};
    mongocxx::client conn{ uri };

    bsoncxx::builder::stream::document document{};

    auto collection = conn["testdb"]["testcollection"];
    document << "hello" << "world";

    collection.insert_one(document.view());
    auto cursor = collection.find({});

    for (auto&& doc : cursor) {
        std::cout << bsoncxx::to_json(doc) << std::endl;
    }
}

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

#include "nosqlupdateoperator.hh"
#include "nosqlbase.hh"
#include "nosql.hh"

using namespace std;

namespace
{

using namespace nosql;

void add_update_path(unordered_set<string>& paths, const string_view& field)
{
    string f = string(field.data(), field.length());

    if (f == "_id")
    {
        throw SoftError("Performing an update on the path '_id' would modify the immutable field '_id'",
                        error::IMMUTABLE_FIELD);
    }

    paths.insert(f);

    auto i = f.find('.');

    if (i != string::npos)
    {
        paths.insert(f.substr(0, i));
    }
}

string check_update_path(const unordered_set<string>& paths, const string_view& field)
{
    string f = string(field.data(), field.length());

    auto it = paths.find(f);

    if (it == paths.end())
    {
        auto i = f.find('.');

        if (i != string::npos)
        {
            it = paths.find(f.substr(0, i));
        }
    }

    if (it != paths.end())
    {
        ostringstream ss;
        ss << "Updating the path '" << field << "' would create a conflict at '" << *it << "'";

        throw SoftError(ss.str(), error::CONFLICTING_UPDATE_OPERATORS);
    }

    return escape_essential_chars(std::move(f));
}

string convert_update_operator_set(const bsoncxx::document::element& element,
                                   const string& doc,
                                   unordered_set<string>& paths)
{
    mxb_assert(element.key().compare("$set") == 0);

    ostringstream ss;

    ss << "JSON_SET(" << doc;

    auto fields = static_cast<bsoncxx::document::view>(element.get_document());

    vector<string_view> svs;
    for (auto field : fields)
    {
        ss << ", ";

        string_view sv = field.key();
        string key = check_update_path(paths, sv);

        ss << "'$." << key << "', " << element_to_value(field, ValueFor::JSON_NESTED);

        svs.push_back(sv);
    }

    for (const auto& sv : svs)
    {
        add_update_path(paths, sv);
    }

    ss << ")";

    return ss.str();
}

string convert_update_operator_unset(const bsoncxx::document::element& element,
                                     const string& doc,
                                     unordered_set<string>& paths)
{
    mxb_assert(element.key().compare("$unset") == 0);

    // The prototype is JSON_REMOVE(doc, path[, path] ...) and if a particular
    // path is not present in the document, there should be no effect. However,
    // there is a bug https://jira.mariadb.org/browse/MDEV-22141 that causes
    // NULL to be returned if a path is not present. To work around that bug,
    // JSON_REMOVE(doc, a, b) is conceptually expressed like:
    //
    // (1) Z = IF(JSON_EXTRACT(doc, a) IS NOT NULL, JSON_REMOVE(doc, a), doc)
    // (2) IF(JSON_EXTRACT(Z, b) IS NOT NULL, JSON_REMOVE(Z, b), Z)
    //
    // and in practice (take a deep breath) so that in (2) every occurence of
    // Z is replaced with the IF-statement at (1). Note that in case there is
    // a third path, then on that iteration, "doc" in (2) will be the entire
    // expression we just got in (2). Also note that the "doc" we start with,
    // may be a JSON-function expression in itself...

    string rv = doc;

    auto fields = static_cast<bsoncxx::document::view>(element.get_document());

    vector<string_view> svs;
    for (auto field : fields)
    {
        string_view sv = field.key();
        string key = escape_essential_chars(string(sv.data(), sv.length()));

        ostringstream ss;

        ss << "IF(JSON_EXTRACT(" << rv << ", '$." << key << "') IS NOT NULL, "
           << "JSON_REMOVE(" << rv << ", '$." << key << "'), " << rv << ")";

        rv = ss.str();

        svs.push_back(sv);
    }

    for (const auto& sv : svs)
    {
        add_update_path(paths, sv);
    }

    return rv;
}

string convert_update_operator_op(const bsoncxx::document::element& element,
                                  const string& doc,
                                  const char* zOperation,
                                  const char* zOp,
                                  unordered_set<string>& paths)
{

    ostringstream ss;

    ss << "JSON_SET(" << doc;

    auto fields = static_cast<bsoncxx::document::view>(element.get_document());

    vector<string_view> svs;
    for (auto field : fields)
    {
        ss << ", ";

        string_view sv = field.key();
        string key = escape_essential_chars(string(sv.data(), sv.length()));

        ss << "'$." << key << "', ";

        double d;
        if (element_as(field, Conversion::RELAXED, &d))
        {
            auto value = double_to_string(d);

            ss << "IF(JSON_EXTRACT(doc, '$." + key + "') IS NOT NULL, "
               << "JSON_VALUE(doc, '$." + key + "')" << zOp << value << ", "
               << value
               << ")";
        }
        else
        {
            DocumentBuilder value;
            append(value, key, field);

            ostringstream ss;
            ss << "Cannot " << zOperation << " with non-numeric argument: "
               << bsoncxx::to_json(value.view());

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        svs.push_back(sv);
    }

    for (const auto& sv : svs)
    {
        add_update_path(paths, sv);
    }

    ss << ")";

    return ss.str();
}

string convert_update_operator_inc(const bsoncxx::document::element& element,
                                   const string& doc,
                                   unordered_set<string>& paths)
{
    mxb_assert(element.key().compare("$inc") == 0);

    return convert_update_operator_op(element, doc, "increment", " + ", paths);
}

string convert_update_operator_mul(const bsoncxx::document::element& element,
                                   const string& doc,
                                   unordered_set<string>& paths)
{
    mxb_assert(element.key().compare("$mul") == 0);

    return convert_update_operator_op(element, doc, "multiply", " * ", paths);
}

string convert_update_operator_rename(const bsoncxx::document::element& element,
                                      const string& doc,
                                      unordered_set<string>& paths)
{
    mxb_assert(element.key().compare("$rename") == 0);

    string rv;

    auto fields = static_cast<bsoncxx::document::view>(element.get_document());

    vector<string_view> svs;
    for (auto field : fields)
    {
        auto from = field.key();

        if (field.type() != bsoncxx::type::k_utf8)
        {
            ostringstream ss;
            ss << "The 'to' field for $rename must be a string: "
               << from << ":"
               << element_to_string(element);

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        string_view to = field.get_utf8();

        if (from == to)
        {
            ostringstream ss;
            ss << "The source and target field for $rename must differ: " << from << ": \"" << to << "\"";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        if (from.length() == 0 || to.length() == 0)
        {
            throw SoftError("An empty update path is not valid.", error::CONFLICTING_UPDATE_OPERATORS);
        }

        if (from.front() == '.' || from.back() == '.' || to.front() == '.' || to.back() == '.')
        {
            string_view path;

            if (from.front() == '.' || from.back() == '.')
            {
                path = from;
            }
            else
            {
                path = to;
            }

            ostringstream ss;
            ss << "The update path '" << path << "' contains an empty field name, which is not allowed.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto from_parts = mxb::strtok(string(from.data(), from.length()), ".");
        auto to_parts = mxb::strtok(string(to.data(), to.length()), ".");

        from_parts.resize(std::min(from_parts.size(), to_parts.size()));

        auto it = from_parts.begin();
        auto jt = to_parts.begin();

        while (*it == *jt && it != from_parts.end())
        {
            ++it;
            ++jt;
        }

        if (jt == to_parts.end())
        {
            ostringstream ss;
            ss << "The source and target field for $rename must not be on the same path: "
               << from << ": \"" << to << "\"";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        if (from.find('$') != string::npos)
        {
            ostringstream ss;
            ss << "The source field for $rename may not be dynamic: " << from;

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        if (to.find('$') != string::npos)
        {
            ostringstream ss;
            ss << "The destination field for $rename may not be dynamic: " << to;

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        string t = check_update_path(paths, to);
        string f = check_update_path(paths, from);

        if (rv.empty())
        {
            rv = "doc";
        }

        ostringstream ss;

        string json_set;

        if (to_parts.size() == 1)
        {
            ostringstream ss2;
            ss2 << "JSON_SET(" << rv << ", '$." << t << "', JSON_EXTRACT(" << rv << ", '$." << f << "'))";
            json_set = ss2.str();
        }
        else
        {
            using Generate = std::function<void(ostream&,
                                                const string&,
                                                const string&,
                                                vector<string>::iterator&,
                                                const vector<string>::iterator&)>;

            Generate generate = [&generate](ostream& out,
                                           const string& rv,
                                           const string& f,
                                           vector<string>::iterator& it,
                                           const vector<string>::iterator& end)
            {
                if (it != end)
                {
                    out << "\"" << *it << "\", JSON_OBJECT(";

                    ++it;

                    generate(out, rv, f, it, end);

                    out << ")";
                }
                else
                {
                    out << "\"" << *it << "\", JSON_EXTRACT(" << rv << ", '$." << f << "')";
                }
            };

            ostringstream ss2;

            // If we have something like '{$rename: {'a.b': 'a.c'}', by explicitly checking whether
            // 'a' is an object, we will end up renaming 'a.b' to 'a.c' (i.e. copy value at 'a.b' to 'a.c'
            // and then delete 'a.b') instead of changing the value of 'a' to '{ c: ... }'. The difference
            // is significant if the document at 'a' contains other fields in addition to 'b'.
            // TODO: This should actually be done for every level.
            string parent_of_t = t.substr(0, t.find_last_of('.'));

            ss2 << "IF(JSON_QUERY(" << rv << ", '$." << parent_of_t << "') IS NOT NULL, "
                << "JSON_SET(" << rv << ", '$." << t << "', JSON_EXTRACT(" << rv << ", '$." << f << "'))"
                << ", "
                << "JSON_SET(" << rv << ", ";

            vector<string> parts = mxb::strtok(t, ".");

            auto it = parts.begin();
            auto end = parts.end() - 1;

            ss2 << "'$." << *it << "', JSON_OBJECT(";

            ++it;

            generate(ss2, rv, f, it, end);

            ss2 << ")))";

            json_set = ss2.str();
        }

        ss << "IF(JSON_EXTRACT(" << rv << ", '$." << f << "') IS NOT NULL, "
           << "JSON_REMOVE(" << json_set << ", '$." << f << "'), "
           << rv << ")";

        rv = ss.str();

        svs.push_back(from);
        svs.push_back(to);
    }

    for (const auto& sv : svs)
    {
        add_update_path(paths, sv);
    }

    return rv;
}

using UpdateOperatorConverter = std::string (*)(const bsoncxx::document::element& element,
                                                const std::string& doc,
                                                std::unordered_set<std::string>& paths);

unordered_map<string, UpdateOperatorConverter> update_operator_converters =
{
    { "$set",    convert_update_operator_set },
    { "$unset",  convert_update_operator_unset },
    { "$inc",    convert_update_operator_inc },
    { "$mul",    convert_update_operator_mul },
    { "$rename", convert_update_operator_rename }
};

string convert_update_operations(const bsoncxx::document::view& update_operations)
{
    string rv;

    unordered_set<string> paths;

    for (auto element : update_operations)
    {
        if (rv.empty())
        {
            rv = "doc";
        }

        auto key = element.key();
        auto it = update_operator_converters.find(string(key.data(), key.length()));
        mxb_assert(it != update_operator_converters.end());

        rv = it->second(element, rv, paths);
    }

    rv += " ";

    return rv;
}

}

namespace nosql
{

bool update_operator::is_supported(const std::string& name)
{
    return update_operator_converters.find(name) != update_operator_converters.end();
}

std::vector<std::string> update_operator::supported_operators()
{
    vector<string> operators;
    for (auto kv: update_operator_converters)
    {
        operators.push_back(kv.first);
    }

    return operators;
}

std::string update_operator::convert(const bsoncxx::document::view& update_operators)
{
    return convert_update_operations(update_operators);
}

}

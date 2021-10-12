#include <iostream>
#include <stdexcept>

#include <maxbase/assert.h>
#include <maxscale/paths.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/modutil.hh>

using namespace std::literals::string_literals;

int errors = 0;

#define expect(a, fmt, ...)                    \
    do{if (!(a)) {                             \
           const char* what = #a;              \
           printf(fmt, ##__VA_ARGS__);         \
           printf("\n");                       \
           ++errors;                           \
       }}while(false)

class Tester
{
public:
    Tester(const char* query_classifier)
    {
        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (!mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            throw std::runtime_error("Failed to initialize the log");
        }

        m_qc = load_classifier(query_classifier);

        if (!m_qc)
        {
            throw std::runtime_error("Failed to load "s + query_classifier);
        }
    }

    ~Tester()
    {
        if (m_qc)
        {
            qc_unload(m_qc);
        }
    }

    qc_query_op_t get_operation(const std::string& sql)
    {
        mxs::Buffer buffer(modutil_create_query(sql.c_str()));
        int32_t op = QUERY_OP_UNDEFINED;

        if (m_qc->qc_get_operation(buffer.get(), &op) != QC_RESULT_OK)
        {
            std::cout << "failed to get operation for: " << sql << std::endl;
        }

        return (qc_query_op_t)op;
    }

    uint32_t get_type(const std::string& sql)
    {
        mxs::Buffer buffer(modutil_create_query(sql.c_str()));
        uint32_t type = 0;

        if (m_qc->qc_get_type_mask(buffer.get(), &type) != QC_RESULT_OK)
        {
            std::cout << "failed to get type for: " << sql << std::endl;
        }

        return type;
    }

private:

    QUERY_CLASSIFIER* load_classifier(const char* name)
    {
        std::string libdir = "../"s + name;
        mxs::set_libdir(libdir.c_str());
        QUERY_CLASSIFIER* pClassifier = qc_load(name);

        if (pClassifier)
        {
            const char* args = "parse_as=10.3,log_unrecognized_statements=1";

            if (pClassifier->qc_setup(QC_SQL_MODE_DEFAULT, args) != QC_RESULT_OK
                || pClassifier->qc_thread_init() != QC_RESULT_OK)
            {
                std::cerr << "error: Could not setup or init classifier " << name << "." << std::endl;
                qc_unload(pClassifier);
                pClassifier = nullptr;
            }
            else
            {
                uint64_t version = 10 * 1000 * 3 * 100;
                pClassifier->qc_set_server_version(version);
            }
        }
        else
        {
            std::cerr << "error: Could not load classifier " << name << "." << std::endl;
        }

        return pClassifier;
    }

    QUERY_CLASSIFIER* m_qc = nullptr;
};

static std::vector<std::tuple<std::string, uint32_t, qc_query_op_t>> test_cases
{
    // TODO: Add test cases
};

int main(int argc, char** argv)
{
    int rc = 0;

    try
    {
        Tester tester("qc_sqlite");

        for (const auto& t : test_cases)
        {
            std::string sql;
            uint32_t expected_type;
            qc_query_op_t expected_op;
            std::tie(sql, expected_type, expected_op) = t;

            auto op = tester.get_operation(sql);
            expect(op == expected_op, "Expected %s, got %s for: %s",
                   qc_op_to_string(expected_op), qc_op_to_string(op), sql.c_str());

            auto type = tester.get_type(sql);
            char* type_str = qc_typemask_to_string(type);
            char* expected_type_str = qc_typemask_to_string(expected_type);

            expect(type == expected_type, "Expected %s, got %s for: %s",
                   expected_type_str, type_str, sql.c_str());

            free(type_str);
            free(expected_type_str);
        }

        rc = errors;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        rc = 1;
    }

    return rc;
}

#include <cstdint>
#include <string>
#include <tr1/memory>
#include <vector>
#include <algorithm>
#include <jansson.h>

/** Request format flags */
#define CDC_REQUEST_TYPE_JSON (1 << 0)
#define CDC_REQUEST_TYPE_AVRO (1 << 1)

namespace CDC
{

// The typedef for the Row type
class InternalRow;
typedef std::tr1::shared_ptr<InternalRow> Row;

typedef std::vector<std::string> ValueList;

// A class that represents a CDC connection
class Connection
{
public:
    Connection(const std::string& address,
               uint16_t port,
               const std::string& user,
               const std::string& password,
               uint32_t flags = CDC_REQUEST_TYPE_JSON);
    virtual ~Connection();
    bool createConnection();
    bool requestData(const std::string& table, const std::string& gtid = "");
    Row read();
    void closeConnection();
    const std::string& getSchema() const
    {
        return m_schema;
    }
    const std::string& getError() const
    {
        return m_error;
    }

private:
    int m_fd;
    uint32_t m_flags;
    uint16_t m_port;
    std::string m_address;
    std::string m_user;
    std::string m_password;
    std::string m_error;
    std::string m_schema;
    ValueList m_keys;
    ValueList m_types;

    bool doAuth();
    bool doRegistration();
    bool readRow(std::string& dest);
    void processSchema(json_t* json);
    Row processRow(json_t*);
};

// Internal representation of a row, used via the Row type
class InternalRow
{
public:

    size_t fieldCount() const
    {
        return m_values.size();
    }

    const std::string& value(size_t i) const
    {
        return m_values[i];
    }

    const std::string& value(const std::string& str) const
    {
        ValueList::const_iterator it = std::find(m_keys.begin(), m_keys.end(), str);
        return m_values[it - m_keys.begin()];
    }

    const std::string& key(size_t i) const
    {
        return m_keys[i];
    }

    const std::string& type(size_t i) const
    {
        return m_types[i];
    }

    ~InternalRow()
    {
    }

private:
    ValueList m_keys;
    ValueList m_types;
    ValueList m_values;

    // Not intended to be copied
    InternalRow(const InternalRow&);
    InternalRow& operator=(const InternalRow&);
    InternalRow();

    // Only a Connection should construct an InternalRow
    friend class Connection;

    InternalRow(const ValueList& keys,
                const ValueList& types,
                const ValueList& values):
        m_keys(keys),
        m_types(types),
        m_values(values)
    {
    }

};

}

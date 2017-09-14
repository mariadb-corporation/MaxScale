#include <cstdint>
#include <string>

/** Request format flags */
#define CDC_REQUEST_TYPE_JSON (1 << 0)
#define CDC_REQUEST_TYPE_AVRO (1 << 1)

namespace CDC
{

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
    bool readRow(std::string& dest);
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

    bool doAuth();
    bool doRegistration();
};

}

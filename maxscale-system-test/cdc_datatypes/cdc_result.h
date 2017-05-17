#ifndef CDC_RESULT_H
#define CDC_RESULT_H

#include <jansson.h>
#include <string>

#ifdef __cplusplus
extern "C"
{
#endif

class TestOutput
{
public:
    TestOutput(const std::string& input, const std::string& name);
    const std::string& getValue() const;

private:
    std::string m_value;
};

class TestInput
{
public:
    TestInput(const std::string& value, const std::string& type, const std::string& name = "a");
    const std::string& getName() const
    {
        return m_name;
    }
    const std::string& getValue() const
    {
        return m_value;
    }
    const std::string& getType() const
    {
        return m_type;
    }

    bool operator ==(const TestOutput& output) const
    {
        return m_value == output.getValue();
    }

    bool operator !=(const TestOutput& output) const
    {
        return !(*this == output);
    }

private:
    std::string m_value;
    std::string m_type;
    std::string m_name;
};

#ifdef __cplusplus
}
#endif

#endif /* CDC_RESULT_H */


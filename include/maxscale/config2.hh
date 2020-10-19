/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/log.hh>
#include <maxscale/config.hh>
#include <maxscale/modinfo.h>

namespace config
{

class Configuration;
class Param;
class Type;

// An instance of Specification specifies what parameters a particular module expects
// and of what type they are.
class Specification
{
public:
    enum Kind
    {
        FILTER,
        MONITOR,
        ROUTER
    };

    using ParamsByName = std::map<std::string, Param*>;
    using const_iterator = ParamsByName::const_iterator;
    using value_type = ParamsByName::value_type;

    /**
     * Constructor
     *
     * @param zModule The the name of the module, e.g. "cachefilter".
     */
    Specification(const char* zModule, Kind kind);
    ~Specification();

    /**
     * @return What kind of specification.
     */
    Kind kind() const;

    /**
     * @return The module name of this specification.
     */
    const std::string& module() const;

    /**
     *  Validate parameters
     *
     * @param params  Parameters as found in the configuration file.
     *
     * @return True, if they represent valid parameters - all mandatory are present,
     *         all present ones are of corrent type - for this configuration.
     */
    bool validate(const MXS_CONFIG_PARAMETER& params) const;

    /**
     * Configure configuration
     *
     * @param configuration  The configuration that should be configured.
     * @param params         The parameters that should be used, will be validated.
     *
     * @return True if could be configured.
     */
    bool configure(Configuration& configuration, const MXS_CONFIG_PARAMETER& params) const;

    /**
     * Find given parameter of the specification.
     *
     * @param name  The name of the parameter.
     *
     * @return The corresponding parameter object or NULL if the name is not a
     *         parameter of the specification.
     */
    const Param* find_param(const std::string& name) const;

    /**
     * Document this specification.
     *
     * @param out  The stream the documentation should be written to.
     *
     * @return @c out
     */
    std::ostream& document(std::ostream& out) const;

    /**
     * Populate legacy parameter definition.
     *
     * @note Only for a transitionary period.
     *
     * @param module  The module description to be populated with parameters.
     */
    void populate(MXS_MODULE& module) const;

    /**
     * @return The number of parameters in the specification.
     */
    size_t size() const;

    /**
     * @return Const iterator to first parameter.
     */
    const_iterator cbegin() const
    {
        return m_params.cbegin();
    }

    /**
     * @return Const iterator to one past last parameter.
     */
    const_iterator cend() const
    {
        return m_params.cend();
    }

private:
    friend Param;

    void insert(Param* pParam);
    void remove(Param* pParam);

private:
    std::string  m_module;
    Kind         m_kind;
    ParamsByName m_params;
};


/**
 * A instance of Param specifies a parameter of a module, that is, its name,
 * type, default value and whether it is mandatory or optional.
 */
class Param
{
public:
    enum Kind
    {
        MANDATORY,
        OPTIONAL
    };

    ~Param();

    /**
     * @return The name of the parameter.
     */
    const std::string& name() const;

    /**
     * @return The type of the parameter (human readable).
     */
    virtual std::string type() const = 0;

    /**
     * @return The description of the parameter.
     */
    const std::string& description() const;

    /**
     * Document the parameter.
     *
     * The documentation of a parameters consists of its name, its type,
     * whether it is mandatory or optional (default value documented in
     * that case), and its description.
     *
     * @return The documentation.
     */
    std::string documentation() const;

    /**
     * @return The kind - mandatory or optional - of the parameter.
     */
    Kind kind() const;

    /**
     * @return True, if the parameter is mandatory.
     */
    bool is_mandatory() const;

    /**
     * @return True, if the parameter is optional.
     */
    bool is_optional() const;

    /**
     * Synonym for @c is_optional.
     *
     * @return True, if the parameter has a default value.
     */
    bool has_default_value() const;

    /**
     * @return Default value as string.
     *
     * @note Meaningful only if @c has_default_value returns true.
     */
    virtual std::string default_to_string() const = 0;

    /**
     * Validate a string.
     *
     * @param value_as_string  The string to validate.
     *
     * @return True, if @c value_as_string can be converted into a value of this type.
     */
    virtual bool validate(const std::string& value_as_string, std::string* pMessage) const = 0;

    /**
     * Set setting value with value from configuration file.
     *
     * @param value            The @c Type to configure.
     * @param value_as_string  The string value to configure it with.
     *
     * @return True, if it could be configured, false otherwise. The
     *         function will fail only if @c value_as_string is invalid.
     */
    virtual bool set(Type& value, const std::string& value_as_string) const = 0;

    /**
     * Populate a legacy parameter specification with data.
     *
     * @param param  The legacy parameter specification to be populated.
     */
    virtual void populate(MXS_MODULE_PARAM& param) const;

protected:
    Param(Specification* pSpecification,
          const char* zName,
          const char* zDescription,
          Kind kind,
          mxs_module_param_type legacy_type);

private:
    Specification&        m_specification;
    std::string           m_name;
    std::string           m_description;
    Kind                  m_kind;
    mxs_module_param_type m_legacy_type;
};

/**
 * ParamBool
 */
class ParamBool : public Param
{
public:
    using value_type = bool;

    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription)
        : ParamBool(pSpecification, zName, zDescription, Param::MANDATORY, value_type())
    {
    }

    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value)
        : ParamBool(pSpecification, zName, zDescription, Param::OPTIONAL, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;

private:
    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Kind kind,
              value_type default_value)
        : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_BOOL)
        , m_default_value(default_value)
    {
    }

private:
    value_type m_default_value;
};

class ParamNumber : public Param
{
public:
    using value_type = int64_t;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;

protected:
    ParamNumber(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Kind kind,
                mxs_module_param_type legacy_type,
                value_type default_value,
                value_type min_value,
                value_type max_value)
        : Param(pSpecification, zName, zDescription, kind, legacy_type)
        , m_default_value(default_value)
        , m_min_value(min_value <= max_value ? min_value : max_value)
        , m_max_value(max_value)
    {
        mxb_assert(min_value <= max_value);
    }

private:
    value_type m_default_value;
    value_type m_min_value;
    value_type m_max_value;
};

/**
 * ParamCount
 */
class ParamCount : public ParamNumber
{
public:
    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription)
        : ParamCount(pSpecification, zName, zDescription, Param::MANDATORY,
                     value_type(), 0, std::numeric_limits<uint32_t>::max())
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type min_value,
               value_type max_value)
        : ParamCount(pSpecification, zName, zDescription, Param::MANDATORY,
                     value_type(), min_value, max_value)
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type default_value)
        : ParamCount(pSpecification, zName, zDescription, Param::OPTIONAL,
                     default_value, 0, std::numeric_limits<uint32_t>::max())
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type default_value,
               value_type min_value,
               value_type max_value)
        : ParamCount(pSpecification, zName, zDescription, Param::OPTIONAL,
                     default_value, min_value, max_value)
    {
    }

    std::string type() const override;

private:
    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               Kind kind,
               value_type default_value,
               value_type min_value,
               value_type max_value)
        : ParamNumber(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_COUNT,
                      default_value,
                      min_value >= 0 ? min_value : 0,
                      max_value <= std::numeric_limits<uint32_t>::max() ?
                      max_value : std::numeric_limits<uint32_t>::max())
    {
        mxb_assert(min_value >= 0);
        mxb_assert(max_value <= std::numeric_limits<uint32_t>::max());
    }
};

using ParamNatural = ParamCount;

/**
 * ParamInteger
 */
class ParamInteger : public ParamNumber
{
public:
    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription)
        : ParamInteger(pSpecification, zName, zDescription, Param::MANDATORY,
                       value_type(),
                       std::numeric_limits<int32_t>::min(),
                       std::numeric_limits<int32_t>::max())
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type min_value,
                 value_type max_value)
        : ParamInteger(pSpecification, zName, zDescription, Param::MANDATORY,
                       value_type(), min_value, max_value)
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type default_value)
        : ParamInteger(pSpecification, zName, zDescription, Param::OPTIONAL,
                       default_value,
                       std::numeric_limits<int32_t>::min(),
                       std::numeric_limits<int32_t>::max())
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type default_value,
                 value_type min_value,
                 value_type max_value)
        : ParamInteger(pSpecification, zName, zDescription, Param::OPTIONAL,
                       default_value, min_value, max_value)
    {
    }

    std::string type() const override;

private:
    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 Kind kind,
                 value_type default_value,
                 value_type min_value,
                 value_type max_value)
        : ParamNumber(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_INT,
                      default_value,
                      min_value >= std::numeric_limits<int32_t>::min() ?
                      min_value : std::numeric_limits<int32_t>::min(),
                      max_value <= std::numeric_limits<int32_t>::max() ?
                      max_value : std::numeric_limits<int32_t>::max())
    {
        mxb_assert(min_value >= std::numeric_limits<int32_t>::min());
        mxb_assert(max_value <= std::numeric_limits<int32_t>::max());
    }
};

/**
 * ParamDuration
 */
template<class T>
class ParamDuration : public Param
{
public:
    using value_type = T;

    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  mxs::config::DurationInterpretation interpretation)
        : ParamDuration(pSpecification, zName, zDescription, Param::MANDATORY, interpretation, value_type())
    {
    }

    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  mxs::config::DurationInterpretation interpretation,
                  value_type default_value)
        : ParamDuration(pSpecification, zName, zDescription, Param::OPTIONAL, interpretation, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(const value_type& value) const;

private:
    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  Kind kind,
                  mxs::config::DurationInterpretation interpretation,
                  value_type default_value)
        : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_DURATION)
        , m_interpretation(interpretation)
        , m_default_value(default_value)
    {
    }

private:
    mxs::config::DurationInterpretation m_interpretation;
    value_type                          m_default_value;
};

/**
 * ParamEnum
 */
template<class T>
class ParamEnum : public Param
{
public:
    using value_type = T;

    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              const std::vector<std::pair<T, const char*>>& enumeration)
        : ParamEnum(pSpecification, zName, zDescription, Param::MANDATORY, enumeration, value_type())
    {
    }

    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              const std::vector<std::pair<T, const char*>>& enumeration,
              value_type default_value)
        : ParamEnum(pSpecification, zName, zDescription, Param::OPTIONAL, enumeration, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;

    void populate(MXS_MODULE_PARAM& param) const;

private:
    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Kind kind,
              const std::vector<std::pair<T, const char*>>& enumeration,
              value_type default_value);

private:
    std::vector<std::pair<T, const char*>> m_enumeration;
    value_type                             m_default_value;
    std::vector<MXS_ENUM_VALUE>            m_enum_values;
};

/**
 * ParamPath
 */
class ParamPath : public Param
{
public:
    using value_type = std::string;

    enum Options
    {
        X = MXS_MODULE_OPT_PATH_X_OK,   // Execute permission required.
        R = MXS_MODULE_OPT_PATH_R_OK,   // Read permission required.
        W = MXS_MODULE_OPT_PATH_W_OK,   // Write permission required.
        F = MXS_MODULE_OPT_PATH_F_OK,   // File existence required.
        C = MXS_MODULE_OPT_PATH_CREAT   // Create path if does not exist.
    };

    const uint32_t MASK = X | R | W | F | C;


    ParamPath(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              uint32_t options)
        : ParamPath(pSpecification, zName, zDescription, Param::MANDATORY, options, value_type())
    {
    }

    ParamPath(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              uint32_t options,
              value_type default_value)
        : ParamPath(pSpecification, zName, zDescription, Param::OPTIONAL, options, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(const value_type& value) const;

    void populate(MXS_MODULE_PARAM& param) const;

private:
    ParamPath(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Kind kind,
              uint32_t options,
              value_type default_value)
        : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_PATH)
        , m_options(options)
        , m_default_value(default_value)
    {
    }

private:
    uint32_t   m_options;
    value_type m_default_value;
};

/**
 * ParamServer
 */
class ParamServer : public Param
{
public:
    using value_type = SERVER*;

    ParamServer(Specification* pSpecification,
                const char* zName,
                const char* zDescription)
        : Param(pSpecification, zName, zDescription, Param::MANDATORY, MXS_MODULE_PARAM_SERVER)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;
};

/**
 * ParamSize
 */
class ParamSize : public Param
{
public:
    using value_type = uint64_t;

    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription)
        : ParamSize(pSpecification, zName, zDescription, Param::MANDATORY, value_type())
    {
    }

    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value)
        : ParamSize(pSpecification, zName, zDescription, Param::OPTIONAL, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;

private:
    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Kind kind,
              value_type default_value)
        : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_SIZE)
        , m_default_value(default_value)
    {
    }

private:
    value_type m_default_value;
};

/**
 * ParamString
 */
class ParamString : public Param
{
public:
    using value_type = std::string;

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription)
        : ParamString(pSpecification, zName, zDescription, Param::MANDATORY, value_type())
    {
    }

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                value_type default_value)
        : ParamString(pSpecification, zName, zDescription, Param::OPTIONAL, default_value)
    {
    }

    std::string type() const override;

    std::string default_to_string() const override;

    bool validate(const std::string& value_as_string, std::string* pMessage) const override;

    bool set(Type& value, const std::string& value_as_string) const override;

    bool from_string(const std::string& value, value_type* pValue,
                     std::string* pMessage = nullptr) const;
    std::string to_string(value_type value) const;

private:
    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Kind kind,
                value_type default_value)
        : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_STRING)
        , m_default_value(default_value)
    {
    }

private:
    value_type m_default_value;
};

/**
 * ParamBitMask
 */
using ParamBitMask = ParamCount;

/**
 * An instance of the class Configuration specifies the configuration of a particular
 * instance of a module.
 *
 * Walks hand in hand with Specification.
 */
class Configuration
{
public:
    using ValuesByName = std::map<std::string, Type*>;
    using const_iterator = ValuesByName::const_iterator;
    using value_type = ValuesByName::value_type;

    /**
     * Constructor
     *
     * @param name            The object (i.e. section name) of this configuration.
     * @param pSpecification  The specification this instance is a configuration of.
     */
    Configuration(const std::string& name, const Specification* pSpecification);

    /**
     * @return The The object (i.e. section name) of this configuration.
     */
    const std::string& name() const;

    /**
     * @return The specification of this configuration.
     */
    const Specification& specification() const;

    /**
     * @param name  The name of the parameter to look up.
     *
     * @return The corresponding @c Value or NULL if @c name is unknown.
     */
    Type*       find_value(const std::string& name);
    const Type* find_value(const std::string& name) const;

    /**
     * Persist the configuration to a stream.
     *
     * @param out  The stream to persist to.
     */
    std::ostream& persist(std::ostream& out) const;

    /**
     * Called when configuration has initially been configured, to allow a
     * Configuration to check any interdependencies between values or to calculate
     * derived ones.
     *
     * @param params  The parameters the configuration was configured with.
     *
     * @return True, if everything is ok.
     *
     * @note The default implementation returns true.
     */
    virtual bool post_configure(const MXS_CONFIG_PARAMETER& params);

    /**
     * @return The number of values in the configuration.
     */
    size_t size() const;

    /**
     * @return Const iterator to first parameter.
     */
    const_iterator cbegin() const
    {
        return m_values.cbegin();
    }

    /**
     * @return Const iterator to one past last parameter.
     */
    const_iterator cend() const
    {
        return m_values.cend();
    }

private:
    friend Type;

    void insert(Type* pValue);
    void remove(Type* pValue, const std::string& name);

private:
    std::string          m_name;
    const Specification& m_specification;
    ValuesByName         m_values;
};


/**
 * Base-class of all configuration value types.
 *
 * In the description of this class, "value" should be read as
 * "an instance of this type".
 */
class Type
{
public:
    Type(const Type& rhs) = delete;
    Type& operator=(const Type&) = delete;

    ~Type();

    /**
     * Get parameter describing this value.
     *
     * @return Param of the value.
     */
    const Param& parameter() const;

    /**
     * Persist this value to a stream. It will be written as
     *
     *    name=value
     *
     * where @c value will be formatted in the correct way.
     *
     * @param out  The stream to write to.
     *
     * @return @c out.
     */
    std::ostream& persist(std::ostream& out) const;

    /**
     * Convert this value into its string representation.
     *
     * @return The value as it should appear in a configuration file.
     */
    virtual std::string to_string() const = 0;

    /**
     * Set value.
     *
     * @param value_as_string  The new value expressed as a string.
     *
     * @return True, if the value could be set, false otherwise.
     */
    bool set(const std::string& value_as_string);

protected:
    Type(Configuration* pConfiguration, const Param* pParam);

private:
    Configuration&    m_configuration;
    const Param&      m_param;
    const std::string m_name;
};

/**
 * A concrete Value. Instantiated with a derived class and the
 * corresponding param type.
 */
template<class This, class ParamType>
class ConcreteType : public Type
{
public:
    using value_type = typename ParamType::value_type;

    ConcreteType(const ConcreteType&) = delete;

    ConcreteType(Configuration* pConfiguration, const ParamType* pParam)
        : Type(pConfiguration, pParam)
    {
    }

    This& operator=(const value_type& value)
    {
        m_value = value;
        return static_cast<This&>(*this);
    }

    This& operator=(const ConcreteType<This, ParamType>& rhs)
    {
        // Only the value is copied, the parameter and the configuration
        // remains the same.
        m_value = rhs.m_value;
        return static_cast<This&>(*this);
    }

    value_type get() const
    {
        return m_value;
    }

    void set(const value_type& value)
    {
        m_value = value;
    }

    std::string to_string() const override
    {
        return static_cast<const ParamType&>(parameter()).to_string(m_value);
    }

protected:
    value_type m_value;
};

/**
 * Comparison operators:
 *
 *   ConcreteType  <-> ConcreteType
 */
template<class This, class ParamType>
inline bool operator==(const ConcreteType<This, ParamType>& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return lhs.get() == rhs.get();
}

template<class This, class ParamType>
inline bool operator!=(const ConcreteType<This, ParamType>& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return lhs.get() != rhs.get();
}

template<class This, class ParamType>
inline bool operator<(const ConcreteType<This, ParamType>& lhs,
                      const ConcreteType<This, ParamType>& rhs)
{
    return lhs.get() < rhs.get();
}

template<class This, class ParamType>
inline bool operator>(const ConcreteType<This, ParamType>& lhs,
                      const ConcreteType<This, ParamType>& rhs)
{
    return lhs.get() > rhs.get();
}

template<class This, class ParamType>
inline bool operator<=(const ConcreteType<This, ParamType>& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return (lhs.get() < rhs.get()) || (lhs == rhs);
}

template<class This, class ParamType>
inline bool operator>=(const ConcreteType<This, ParamType>& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return (lhs.get() > rhs.get()) || (lhs == rhs);
}

/**
 * Comparison operators:
 *
 *   ConcreteType  <-> ParamType::value_type
 */
template<class This, class ParamType>
inline bool operator==(const ConcreteType<This, ParamType>& lhs,
                       const typename ParamType::value_type& rhs)
{
    return lhs.get() == rhs;
}

template<class This, class ParamType>
inline bool operator!=(const ConcreteType<This, ParamType>& lhs,
                       const typename ParamType::value_type& rhs)
{
    return lhs.get() != rhs;
}

template<class This, class ParamType>
inline bool operator<(const ConcreteType<This, ParamType>& lhs,
                      const typename ParamType::value_type& rhs)
{
    return lhs.get() < rhs;
}

template<class This, class ParamType>
inline bool operator>(const ConcreteType<This, ParamType>& lhs,
                      const typename ParamType::value_type& rhs)
{
    return lhs.get() > rhs;
}

template<class This, class ParamType>
inline bool operator<=(const ConcreteType<This, ParamType>& lhs,
                       const typename ParamType::value_type& rhs)
{
    return (lhs.get() < rhs) || (lhs.get() == rhs);
}

template<class This, class ParamType>
inline bool operator>=(const ConcreteType<This, ParamType>& lhs,
                       const typename ParamType::value_type& rhs)
{
    return (lhs.get() > rhs) || (lhs.get() == rhs);
}

/**
 * Comparison operators:
 *
 *   ParamType::value_type <-> ConcreteType
 */
template<class This, class ParamType>
inline bool operator==(const typename ParamType::value_type& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return lhs == rhs.get();
}

template<class This, class ParamType>
inline bool operator!=(const typename ParamType::value_type& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return lhs != rhs.get();
}

template<class This, class ParamType>
inline bool operator<(const typename ParamType::value_type& lhs,
                      const ConcreteType<This, ParamType>& rhs)
{
    return lhs < rhs.get();
}

template<class This, class ParamType>
inline bool operator>(const typename ParamType::value_type& lhs,
                      const ConcreteType<This, ParamType>& rhs)
{
    return lhs > rhs.get();
}

template<class This, class ParamType>
inline bool operator<=(const typename ParamType::value_type& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return (lhs < rhs.get()) || (lhs == rhs.get());
}

template<class This, class ParamType>
inline bool operator>=(const typename ParamType::value_type& lhs,
                       const ConcreteType<This, ParamType>& rhs)
{
    return (lhs > rhs.get()) || (lhs == rhs.get());
}


class Number : public ConcreteType<Number, ParamNumber>
{
protected:
    using ConcreteType<Number, ParamNumber>::operator =;

    Number(Configuration* pConfiguration, const ParamNumber* pParam)
        : ConcreteType(pConfiguration, pParam)
    {
    }
};

/**
 * Count
 */
class Count : public Number
{
public:
    using Number::operator =;

    Count(Configuration* pConfiguration, const ParamCount* pParam)
        : Number(pConfiguration, pParam)
    {
    }
};

/**
 * Integer
 */
class Integer : public Number
{
public:
    using Number::operator =;

    Integer(Configuration* pConfiguration, const ParamInteger* pParam)
        : Number(pConfiguration, pParam)
    {
    }
};

/**
 * BitMask
 */
class BitMask : public Count
{
public:
    using Count::operator =;

    BitMask(Configuration* pConfiguration, const ParamCount* pParam)
        : Count(pConfiguration, pParam)
    {
    }

    bool is_set(value_type bit) const
    {
        return (m_value & bit) == bit;
    }
};

/**
 * Bool
 */
class Bool : public ConcreteType<Bool, ParamBool>
{
public:
    using ConcreteType<Bool, ParamBool>::operator =;

    Bool(Configuration* pConfiguration, const ParamBool* pParam)
        : ConcreteType<Bool, ParamBool>(pConfiguration, pParam)
    {
    }

    explicit operator bool() const
    {
        return m_value;
    }
};

/**
 * Duration
 */
template<class T>
class Duration : public ConcreteType<Duration<T>, ParamDuration<T>>
{
public:
    using ConcreteType<Duration<T>, ParamDuration<T>>::operator =;

    Duration(Configuration* pConfiguration, const ParamDuration<T>* pParam)
        : ConcreteType<Duration<T>, ParamDuration<T>>(pConfiguration, pParam)
    {
    }

    typename T::rep count() const
    {
        return ConcreteType<Duration<T>, ParamDuration<T>>::m_value.count();
    }
};

/*
 *  template<class T>
 *  inline bool operator < (const Duration<T>& lhs, const Duration<T>& rhs)
 *  {
 *   return lhs.get() < rhs.get();
 *  }
 *
 *  template<class T>
 *  inline bool operator > (const Duration<T>& lhs, const Duration<T>& rhs)
 *  {
 *   return lhs.get() > rhs.get();
 *  }
 */

/**
 * Enum
 */
template<class T>
class Enum : public ConcreteType<Enum<T>, ParamEnum<T>>
{
public:
    using ConcreteType<Enum<T>, ParamEnum<T>>::operator =;

    Enum(Configuration* pConfiguration, const ParamEnum<T>* pParam)
        : ConcreteType<Enum<T>, ParamEnum<T>>(pConfiguration, pParam)
    {
    }
};

/**
 * Path
 */
class Path : public ConcreteType<Path, ParamPath>
{
public:
    using ConcreteType<Path, ParamPath>::operator =;

    Path(Configuration* pConfiguration, const ParamPath* pParam)
        : ConcreteType<Path, ParamPath>(pConfiguration, pParam)
    {
    }

    const char* c_str() const
    {
        return m_value.c_str();
    }

    bool empty() const
    {
        return m_value.empty();
    }
};

/**
 * Size
 */
class Size : public ConcreteType<Size, ParamSize>
{
public:
    using ConcreteType<Size, ParamSize>::operator =;

    Size(Configuration* pConfiguration, const ParamSize* pParam)
        : ConcreteType(pConfiguration, pParam)
    {
    }
};

inline Size::value_type operator/(const Size& lhs, Size::value_type rhs)
{
    return lhs.get() / rhs;
}

/**
 * Server
 */
class Server : public ConcreteType<Server, ParamServer>
{
public:
    using ConcreteType<Server, ParamServer>::operator =;

    Server(Configuration* pConfiguration, const ParamServer* pParam)
        : ConcreteType<Server, ParamServer>(pConfiguration, pParam)
    {
    }
};

/**
 * String
 */
class String : public ConcreteType<String, ParamString>
{
public:
    using ConcreteType<String, ParamString>::operator =;

    String(Configuration* pConfiguration, const ParamString* pParam)
        : ConcreteType<String, ParamString>(pConfiguration, pParam)
    {
    }

    const char* c_str() const
    {
        return m_value.c_str();
    }

    bool empty() const
    {
        return m_value.empty();
    }
};

/**
 * IMPLEMENTATION DETAILS
 */
struct DurationSuffix
{
    static const char* of(const std::chrono::seconds&)
    {
        return "s";
    }

    static const char* of(const std::chrono::milliseconds&)
    {
        return "ms";
    }
};

template<class T>
std::string ParamDuration<T>::type() const
{
    return "duration";
}

template<class T>
std::string ParamDuration<T>::default_to_string() const
{
    return to_string(m_default_value);
}

template<class T>
bool ParamDuration<T>::validate(const std::string& value_as_string, std::string* pMessage) const
{
    value_type value;
    return from_string(value_as_string, &value, pMessage);
}

template<class T>
bool ParamDuration<T>::set(Type& value, const std::string& value_as_string) const
{
    mxb_assert(&value.parameter() == this);

    Duration<T>& duration_value = static_cast<Duration<T>&>(value);

    value_type x;
    bool valid = from_string(value_as_string, &x);

    if (valid)
    {
        duration_value.set(x);
    }

    return valid;
}

template<class T>
bool ParamDuration<T>::from_string(const std::string& value_as_string,
                                   value_type* pValue,
                                   std::string* pMessage) const
{
    mxs::config::DurationUnit unit;

    std::chrono::milliseconds duration;
    bool valid = get_suffixed_duration(value_as_string.c_str(), m_interpretation, &duration, &unit);

    if (valid)
    {
        if (unit == mxs::config::DURATION_IN_DEFAULT)
        {
            if (pMessage)
            {
                *pMessage = "Specifying durations without a suffix denoting the unit has been deprecated: ";
                *pMessage += value_as_string;
                *pMessage += ". Use the suffixes 'h' (hour), 'm' (minute) 's' (second) or ";
                *pMessage += "'ms' (milliseconds).";
            }
        }

        *pValue = std::chrono::duration_cast<value_type>(duration);
    }
    else if (pMessage)
    {
        *pMessage = "Invalid duration: ";
        *pMessage += value_as_string;
    }

    return valid;
}

template<class T>
std::string ParamDuration<T>::to_string(const value_type& value) const
{
    std::stringstream ss;
    ss << value.count() << DurationSuffix::of(value);
    return ss.str();
}

template<class T>
ParamEnum<T>::ParamEnum(Specification* pSpecification,
                        const char* zName,
                        const char* zDescription,
                        Kind kind,
                        const std::vector<std::pair<T, const char*>>& enumeration,
                        value_type default_value)
    : Param(pSpecification, zName, zDescription, kind, MXS_MODULE_PARAM_ENUM)
    , m_enumeration(enumeration)
    , m_default_value(default_value)
{
    m_enum_values.reserve(m_enumeration.size() + 1);

    for (const auto& entry : enumeration)
    {
        MXS_ENUM_VALUE x {};
        x.name = entry.second;
        x.enum_value = entry.first;

        m_enum_values.emplace_back(x);
    }

    MXS_ENUM_VALUE end {NULL};
    m_enum_values.emplace_back(end);
}

template<class T>
std::string ParamEnum<T>::type() const
{
    std::string s("enumeration:[");

    bool first = true;
    for (const auto& p : m_enumeration)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            s += ", ";
        }

        s += p.second;
    }

    s += "]";

    return s;
}

template<class T>
std::string ParamEnum<T>::default_to_string() const
{
    return to_string(m_default_value);
}

template<class T>
bool ParamEnum<T>::validate(const std::string& value_as_string, std::string* pMessage) const
{
    value_type value;
    return from_string(value_as_string, &value, pMessage);
}

template<class T>
bool ParamEnum<T>::set(Type& value, const std::string& value_as_string) const
{
    mxb_assert(&value.parameter() == this);

    Enum<T>& enum_value = static_cast<Enum<T>&>(value);

    value_type x;
    bool valid = from_string(value_as_string, &x);

    if (valid)
    {
        enum_value.set(x);
    }

    return valid;
}

template<class T>
bool ParamEnum<T>::from_string(const std::string& value_as_string,
                               value_type* pValue,
                               std::string* pMessage) const
{
    auto it = std::find_if(m_enumeration.begin(), m_enumeration.end(),
                           [value_as_string](const std::pair<T, const char*>& elem) {
                               return value_as_string == elem.second;
                           });

    if (it != m_enumeration.end())
    {
        *pValue = it->first;
    }
    else if (pMessage)
    {
        std::string s;
        for (size_t i = 0; i < m_enumeration.size(); ++i)
        {
            s += "'";
            s += m_enumeration[i].second;
            s += "'";

            if (i == m_enumeration.size() - 2)
            {
                s += " and ";
            }
            else if (i != m_enumeration.size() - 1)
            {
                s += ", ";
            }
        }

        *pMessage = "Invalid enumeration value: ";
        *pMessage += value_as_string;
        *pMessage += ", valid values are: ";
        *pMessage += s;
        *pMessage += ".";
    }

    return it != m_enumeration.end();
}

template<class T>
std::string ParamEnum<T>::to_string(value_type value) const
{
    auto it = std::find_if(m_enumeration.begin(), m_enumeration.end(),
                           [value](const std::pair<T, const char*>& entry) {
                               return entry.first == value;
                           });

    return it != m_enumeration.end() ? it->second : "unknown";
}

template<class T>
void ParamEnum<T>::populate(MXS_MODULE_PARAM& param) const
{
    Param::populate(param);

    param.accepted_values = &m_enum_values[0];
}
}

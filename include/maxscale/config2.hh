/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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
#include <maxscale/config_common.hh>
#include <maxscale/modinfo.hh>

namespace maxscale
{

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
        ROUTER,
        GLOBAL
    };

    using ParamsByName = std::map<std::string, Param*>;     // We want to have them ordered by name.
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
    Kind kind() const
    {
        return m_kind;
    }

    /**
     * @return The module name of this specification.
     */
    const std::string& module() const;

    /**
     *  Validate parameters
     *
     * @param params         Parameters as found in the configuration file.
     * @param pUnrecognized  If non-null:
     *                       - Will contain on return parameters that were not used.
     *                       - An unrecognized parameter will not cause the configuring
     *                         to fail.
     *
     * @return True, if the @params represent valid parameters - all mandatory are
     *         present, all present ones are of corrent type - for this specification.
     */
    virtual bool validate(const mxs::ConfigParameters& params,
                          mxs::ConfigParameters* pUnrecognized = nullptr) const;

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

    /**
     * @return Specification as a json array.
     */
    json_t* to_json() const;

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

    enum Modifiable
    {
        AT_STARTUP,     // The parameter can be modified only at startup.
        AT_RUNTIME      // The parameter can be modified also at runtime.
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
     * @return Modifiable::AT_RUNTIME or Modifiable::AT_STARTUP.
     */
    Modifiable modifiable() const;

    /**
     * @return True, if the parameter can be modified at runtime.
     */
    bool is_modifiable_at_runtime() const
    {
        return m_modifiable == Modifiable::AT_RUNTIME;
    }

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
     * Populate a legacy parameter specification with data.
     *
     * @param param  The legacy parameter specification to be populated.
     */
    virtual void populate(MXS_MODULE_PARAM& param) const;

    /**
     * @return Parameter as json object.
     */
    virtual json_t* to_json() const;

protected:
    Param(Specification* pSpecification,
          const char* zName,
          const char* zDescription,
          Modifiable modifiable,
          Kind kind,
          mxs_module_param_type legacy_type);

private:
    Specification&        m_specification;
    std::string           m_name;
    std::string           m_description;
    Modifiable            m_modifiable;
    Kind                  m_kind;
    mxs_module_param_type m_legacy_type;
};

/**
 * Concrete Param, helper class to be derived from with the actual
 * concrete parameter class.
 */
template<class ParamType, class NativeType>
class ConcreteParam : public Param
{
public:
    using value_type = NativeType;

    value_type default_value() const
    {
        return m_default_value;
    }

    std::string default_to_string() const override
    {
        return static_cast<const ParamType*>(this)->to_string(m_default_value);
    }

    bool validate(const std::string& value_as_string, std::string* pMessage) const override
    {
        value_type value;
        return static_cast<const ParamType*>(this)->from_string(value_as_string, &value, pMessage);
    }

    bool is_valid(const value_type&) const
    {
        return true;
    }

    /**
     * Returns the value of this parameter as specified in the provided
     * collection of parameters, or default value if none specified.
     *
     * @note Before calling this member function @params should have been
     *       validated by calling @c Specification::validate(params).
     *
     * @params The provided configuration params.
     *
     * @return The value of this parameter.
     */
    value_type get(const mxs::ConfigParameters& params) const
    {
        value_type rv {m_default_value};

        bool contains = params.contains(name());
        mxb_assert(!is_mandatory() || contains);

        if (contains)
        {
            const ParamType* pThis = static_cast<const ParamType*>(this);

            MXB_AT_DEBUG(bool valid = ) pThis->from_string(params.get_string(name()), &rv);
            mxb_assert(valid);
        }

        return rv;
    }

protected:
    ConcreteParam(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  Modifiable modifiable,
                  Kind kind,
                  mxs_module_param_type legacy_type,
                  value_type default_value)
        : Param(pSpecification, zName, zDescription, modifiable, kind, legacy_type)
        , m_default_value(default_value)
    {
    }

    value_type m_default_value;
};

/**
 * ParamBool
 */
class ParamBool : public ConcreteParam<ParamBool, bool>
{
public:
    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamBool(pSpecification, zName, zDescription, modifiable, Param::MANDATORY, value_type())
    {
    }

    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamBool(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL, default_value)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    ParamBool(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable,
              Kind kind,
              value_type default_value)
        : ConcreteParam<ParamBool, bool>(pSpecification, zName, zDescription,
                                         modifiable, kind, MXS_MODULE_PARAM_BOOL, default_value)
    {
    }
};

class ParamNumber : public ConcreteParam<ParamNumber, int64_t>
{
public:
    virtual std::string to_string(value_type value) const;
    virtual bool        from_string(const std::string& value, value_type* pValue,
                                    std::string* pMessage = nullptr) const;

    virtual json_t* to_json(value_type value) const;
    virtual bool    from_json(const json_t* pJson, value_type* pValue,
                              std::string* pMessage = nullptr) const;

    bool is_valid(value_type value) const
    {
        return value >= m_min_value && value <= m_max_value;
    }

    value_type min_value() const
    {
        return m_min_value;
    }

    value_type max_value() const
    {
        return m_max_value;
    }

protected:
    ParamNumber(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable,
                Kind kind,
                mxs_module_param_type legacy_type,
                value_type default_value,
                value_type min_value,
                value_type max_value)
        : ConcreteParam<ParamNumber, int64_t>(pSpecification, zName, zDescription,
                                              modifiable, kind, legacy_type, default_value)
        , m_min_value(min_value <= max_value ? min_value : max_value)
        , m_max_value(max_value)
    {
        mxb_assert(min_value <= max_value);
    }

    bool from_value(value_type value,
                    value_type* pValue,
                    std::string* pMessage) const;

protected:
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
               const char* zDescription,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamCount(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                     value_type(), 0, std::numeric_limits<value_type>::max())
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type min_value,
               value_type max_value,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamCount(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                     value_type(), min_value, max_value)
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type default_value,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamCount(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                     default_value, 0, std::numeric_limits<value_type>::max())
    {
    }

    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               value_type default_value,
               value_type min_value,
               value_type max_value,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamCount(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                     default_value, min_value, max_value)
    {
    }

    std::string type() const override;

private:
    ParamCount(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               Modifiable modifiable,
               Kind kind,
               value_type default_value,
               value_type min_value,
               value_type max_value)
        : ParamNumber(pSpecification, zName, zDescription, modifiable, kind, MXS_MODULE_PARAM_COUNT,
                      default_value,
                      min_value >= 0 ? min_value : 0,
                      max_value <= std::numeric_limits<value_type>::max() ?
                      max_value : std::numeric_limits<value_type>::max())
    {
        mxb_assert(min_value >= 0);
        mxb_assert(max_value <= std::numeric_limits<value_type>::max());
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
                 const char* zDescription,
                 Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamInteger(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                       value_type(),
                       std::numeric_limits<value_type>::min(),
                       std::numeric_limits<value_type>::max())
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type min_value,
                 value_type max_value,
                 Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamInteger(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                       value_type(), min_value, max_value)
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type default_value,
                 Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamInteger(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                       default_value,
                       std::numeric_limits<value_type>::min(),
                       std::numeric_limits<value_type>::max())
    {
    }

    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 value_type default_value,
                 value_type min_value,
                 value_type max_value,
                 Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamInteger(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                       default_value, min_value, max_value)
    {
    }

    std::string type() const override;

private:
    ParamInteger(Specification* pSpecification,
                 const char* zName,
                 const char* zDescription,
                 Modifiable modifiable,
                 Kind kind,
                 value_type default_value,
                 value_type min_value,
                 value_type max_value)
        : ParamNumber(pSpecification, zName, zDescription, modifiable, kind, MXS_MODULE_PARAM_INT,
                      default_value,
                      min_value >= std::numeric_limits<value_type>::min() ?
                      min_value : std::numeric_limits<value_type>::min(),
                      max_value <= std::numeric_limits<value_type>::max() ?
                      max_value : std::numeric_limits<value_type>::max())
    {
        mxb_assert(min_value >= std::numeric_limits<value_type>::min());
        mxb_assert(max_value <= std::numeric_limits<value_type>::max());
    }
};

/**
 * ParamDuration
 */
template<class T>
class ParamDuration : public ConcreteParam<ParamDuration<T>, T>
{
public:
    using value_type = T;

    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  mxs::config::DurationInterpretation interpretation,
                  Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamDuration(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                        interpretation, value_type())
    {
    }

    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  mxs::config::DurationInterpretation interpretation,
                  value_type default_value,
                  Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamDuration(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                        interpretation, default_value)
    {
    }

    std::string type() const override;

    std::string to_string(const value_type& value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(const value_type& value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    ParamDuration(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  Param::Modifiable modifiable,
                  Param::Kind kind,
                  mxs::config::DurationInterpretation interpretation,
                  value_type default_value)
        : ConcreteParam<ParamDuration<T>, T>(pSpecification, zName, zDescription,
                                             modifiable, kind, MXS_MODULE_PARAM_DURATION, default_value)
        , m_interpretation(interpretation)
    {
    }

private:
    mxs::config::DurationInterpretation m_interpretation;
};

using ParamMilliseconds = ParamDuration<std::chrono::milliseconds>;
using ParamSeconds = ParamDuration<std::chrono::seconds>;

/**
 * ParamEnum
 */
template<class T>
class ParamEnum : public ConcreteParam<ParamEnum<T>, T>
{
public:
    using value_type = T;

    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              const std::vector<std::pair<T, const char*>>& enumeration,
              Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamEnum(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                    enumeration, value_type())
    {
    }

    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              const std::vector<std::pair<T, const char*>>& enumeration,
              value_type default_value,
              Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamEnum(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                    enumeration, default_value)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

    void populate(MXS_MODULE_PARAM& param) const;

private:
    ParamEnum(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Param::Modifiable modifiable,
              Param::Kind kind,
              const std::vector<std::pair<T, const char*>>& enumeration,
              value_type default_value);

private:
    std::vector<std::pair<T, const char*>> m_enumeration;
    std::vector<MXS_ENUM_VALUE>            m_enum_values;
};

/**
 * ParamPath
 */
class ParamPath : public ConcreteParam<ParamPath, std::string>
{
public:
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
              uint32_t options,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamPath(pSpecification, zName, zDescription, modifiable, Param::MANDATORY, options, value_type())
    {
    }

    ParamPath(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              uint32_t options,
              value_type default_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamPath(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL, options, default_value)
    {
    }

    std::string type() const override;

    std::string to_string(const value_type& value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(const value_type& value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

    bool is_valid(const value_type& value) const;

    void populate(MXS_MODULE_PARAM& param) const;

private:
    ParamPath(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable,
              Kind kind,
              uint32_t options,
              value_type default_value)
        : ConcreteParam<ParamPath, std::string>(pSpecification, zName, zDescription,
                                                modifiable, kind, MXS_MODULE_PARAM_PATH, default_value)
        , m_options(options)
    {
    }

private:
    uint32_t m_options;
};

/**
 * ParamServer
 */
class ParamServer : public ConcreteParam<ParamServer, SERVER*>
{
public:
    ParamServer(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ConcreteParam<ParamServer, SERVER*>(pSpecification, zName, zDescription,
                                              modifiable, Param::MANDATORY, MXS_MODULE_PARAM_SERVER,
                                              nullptr)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;
};

/**
 * ParamTarget
 */
class ParamTarget : public ConcreteParam<ParamTarget, mxs::Target*>
{
public:
    ParamTarget(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ConcreteParam<ParamTarget, mxs::Target*>(pSpecification, zName, zDescription,
                                                   modifiable, Param::MANDATORY, MXS_MODULE_PARAM_TARGET,
                                                   nullptr)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;
};

/**
 * ParamSize
 */
class ParamSize : public ParamNumber
{
public:
    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamSize(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                    value_type(),
                    0,
                    std::numeric_limits<value_type>::max())
    {
    }

    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type min_value,
              value_type max_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamSize(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                    value_type(),
                    min_value, max_value)
    {
    }

    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamSize(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                    default_value,
                    0,
                    std::numeric_limits<value_type>::max())
    {
    }

    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value,
              value_type min_value,
              value_type max_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamSize(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL, default_value,
                    min_value, max_value)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const override;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    ParamSize(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable,
              Kind kind,
              value_type default_value,
              value_type min_value,
              value_type max_value)
        : ParamNumber(pSpecification, zName, zDescription, modifiable, kind, MXS_MODULE_PARAM_SIZE,
                      default_value, min_value, max_value)
    {
    }
};

/**
 * ParamString
 */
class ParamString : public ConcreteParam<ParamString, std::string>
{
public:
    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, modifiable, Param::MANDATORY, value_type())
    {
    }

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                value_type default_value,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL, default_value)
    {
    }

    std::string type() const override;

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable,
                Kind kind,
                value_type default_value)
        : ConcreteParam<ParamString, std::string>(pSpecification, zName, zDescription,
                                                  modifiable, kind, MXS_MODULE_PARAM_STRING, default_value)
    {
    }
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
    using ValuesByName = std::map<std::string, Type*>;      // We want to have them ordered by name.
    using const_iterator = ValuesByName::const_iterator;
    using value_type = ValuesByName::value_type;

    Configuration(Configuration&& rhs) = default;
    Configuration& operator=(Configuration&& rhs) = default;

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
     * Configure this configuration
     *
     * @param params         The parameters that should be used, will be validated.
     * @param pUnrecognized  If non-null:
     *                       - Will contain on return parameters that were not used.
     *                       - An unrecognized parameter will not cause the configuring
     *                         to fail.
     *
     * @return True if could be configured.
     */
    virtual bool configure(const mxs::ConfigParameters& params,
                           mxs::ConfigParameters* pUnrecognized = nullptr);

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
     * Fill the object with the param-name/param-value pairs of the configuration.
     *
     * @param pJson  The json object to be filled.
     */
    void fill(json_t* pJson) const;

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

    /**
     * @return Return the configuration as a json array.
     */
    json_t* to_json() const;

protected:
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
    virtual bool post_configure(const mxs::ConfigParameters& params);

    /**
     * Add a native parameter value:
     * - will be configured at startup
     * - assumed not to be modified at runtime via admin interface
     *
     * @param pValue  Pointer to the parameter value.
     * @param pParam  Pointer to paramter describing value.
     * @param onSet   Optional functor to be called when value is set (at startup).
     */
    template<class ParamType>
    void add_native(typename ParamType::value_type* pValue,
                    ParamType* pParam,
                    std::function<void(typename ParamType::value_type)> on_set = nullptr);

private:
    friend Type;

    void insert(Type* pValue);
    void remove(Type* pValue, const std::string& name);

private:
    using Natives = std::vector<std::unique_ptr<Type>>;

    std::string          m_name;
    const Specification* m_pSpecification;
    ValuesByName         m_values;
    Natives              m_natives;
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

    // Type is move-only
    Type(Type&& rhs);
    Type& operator=(Type&&);

    virtual ~Type();

    /**
     * Get parameter describing this value.
     *
     * @return Param of the value.
     */
    virtual const Param& parameter() const;

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
     * Convert this value to a json object.
     *
     * @return The value as a json object.
     */
    virtual json_t* to_json() const = 0;

    /**
     * Set value.
     *
     * @param value_as_string  The new value expressed as a string.
     * @param pMessage         If non-null, on failure will contain
     *                         reason why.
     *
     * @return True, if the value could be set, false otherwise.
     */
    virtual bool set_from_string(const std::string& value_as_string,
                                 std::string* pMessage = nullptr) = 0;

    /**
     * Set value.
     *
     * @param json      The new value expressed as a json object.
     * @param pMessage  If non-null, on failure will contain reason why.
     *
     * @return True, if the value could be set, false otherwise.
     */
    virtual bool set_from_json(const json_t* pJson,
                               std::string* pMessage = nullptr) = 0;

protected:
    Type(Configuration* pConfiguration, const Param* pParam);

    Configuration* m_pConfiguration;
    const Param*   m_pParam;
    std::string    m_name;
};

/**
 * Wrapper for native configuration value, not to be instantiated explicitly.
 */
template<class ParamType>
class Native : public Type
{
public:
    using value_type = typename ParamType::value_type;

    Native(const Type& rhs) = delete;
    Native& operator=(const Native&) = delete;

    Native(Configuration* pConfiguration,
           ParamType* pParam,
           value_type* pValue,
           std::function<void(value_type)> on_set = nullptr)
        : Type(pConfiguration, pParam)
        , m_pValue(pValue)
        , m_on_set(on_set)
    {
        // Native values are not modifiable at runtime.
        mxb_assert(!pParam->is_modifiable_at_runtime());
    }

    // Native is move-only
    Native(Native&& rhs)
        : m_pValue(rhs.m_pValue)
    {
        rhs.m_pValue = nullptr;
    }

    Native& operator=(Native&& rhs)
    {
        if (this != &rhs)
        {
            Type::operator=(rhs);
            m_pValue = rhs.m_pValue;
            rhs.m_pValue = nullptr;
        }

        return *this;
    }

    ~Native() = default;

    const ParamType& parameter() const override final
    {
        return static_cast<const ParamType&>(*m_pParam);
    }

    std::string to_string() const override
    {
        return parameter().to_string(*m_pValue);
    }

    json_t* to_json() const override final
    {
        return parameter().to_json(*m_pValue);
    }

    bool set_from_string(const std::string& value_as_string,
                         std::string* pMessage = nullptr) override final
    {
        value_type value;
        bool rv = parameter().from_string(value_as_string, &value, pMessage);

        if (rv)
        {
            rv = set(value);
        }

        return rv;
    }

    bool set_from_json(const json_t* pJson,
                       std::string* pMessage = nullptr) override final
    {
        value_type value;
        bool rv = parameter().from_json(pJson, &value, pMessage);

        if (rv)
        {
            rv = set(value);
        }

        return rv;
    }

    value_type get() const
    {
        return *m_pValue;
    }

    bool set(const value_type& value)
    {
        bool rv = parameter().is_valid(value);

        if (rv)
        {
            *m_pValue = value;

            if (m_on_set)
            {
                m_on_set(value);
            }
        }

        return rv;
    }

protected:
    value_type*                     m_pValue;
    std::function<void(value_type)> m_on_set;
};

/**
 * A concrete Value. Instantiated with a derived class and the
 * corresponding param type.
 */
template<class ParamType>
class ConcreteType : public Type
{
public:
    using value_type = typename ParamType::value_type;

    ConcreteType(const ConcreteType&) = delete;
    ConcreteType& operator=(const ConcreteType& value) = delete;

    ConcreteType(ConcreteType&& rhs)
        : Type(std::forward<ConcreteType &&>(rhs))
        , m_value(std::move(rhs.m_value))
        , m_on_set(std::move(rhs.m_on_set))
    {
    }

    ConcreteType(Configuration* pConfiguration,
                 const ParamType* pParam,
                 std::function<void(value_type)> on_set = nullptr)
        : Type(pConfiguration, pParam)
        , m_value(pParam->default_value())
        , m_on_set(on_set)
    {
    }

    const ParamType& parameter() const override
    {
        return static_cast<const ParamType&>(*m_pParam);
    }

    bool set_from_string(const std::string& value_as_string,
                         std::string* pMessage = nullptr) override
    {
        value_type value;
        bool rv = parameter().from_string(value_as_string, &value, pMessage);

        if (rv)
        {
            rv = set(value);
        }

        return rv;
    }

    bool set_from_json(const json_t* pJson,
                       std::string* pMessage = nullptr) override
    {
        value_type value;
        bool rv = parameter().from_json(pJson, &value, pMessage);

        if (rv)
        {
            rv = set(value);
        }

        return rv;
    }

    value_type get() const
    {
        return parameter().is_modifiable_at_runtime() ? atomic_get() : non_atomic_get();
    }

    bool set(const value_type& value)
    {
        bool rv = parameter().is_valid(value);

        if (rv)
        {
            if (parameter().is_modifiable_at_runtime())
            {
                atomic_set(value);
            }
            else
            {
                non_atomic_set(value);
            }

            if (m_on_set)
            {
                m_on_set(value);
            }
        }

        return rv;
    }

    std::string to_string() const override
    {
        return parameter().to_string(m_value);
    }

    json_t* to_json() const override
    {
        return parameter().to_json(m_value);
    }

protected:
    void non_atomic_set(const value_type& value)
    {
        m_value = value;
    }

    value_type non_atomic_get() const
    {
        return m_value;
    }

    virtual value_type atomic_get() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return non_atomic_get();
    }

    virtual void atomic_set(const value_type& value)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        non_atomic_set(value);
    }

protected:
    value_type                      m_value;
    mutable std::mutex              m_mutex;
    std::function<void(value_type)> m_on_set;
};


template<class ParamType>
class Number : public ConcreteType<ParamType>
{
public:
    using value_type = typename ParamType::value_type;

    Number(Configuration* pConfiguration,
           const ParamType* pParam,
           std::function<void(value_type)> on_set = nullptr)
        : ConcreteType<ParamType>(pConfiguration, pParam, on_set)
    {
    }

protected:
    value_type atomic_get() const override final
    {
        // this-> as otherwise m_value is not visible.
        return mxb::atomic::load(&this->m_value, mxb::atomic::RELAXED);
    }

    void atomic_set(const value_type& value) override final
    {
        mxb::atomic::store(&this->m_value, value, mxb::atomic::RELAXED);
    }
};

/**
 * Count
 */
using Count = Number<ParamCount>;

/**
 * Integer
 */
using Integer = Number<ParamInteger>;

/**
 * BitMask
 */
using BitMask = Count;

/**
 * Bool
 */
using Bool = ConcreteType<ParamBool>;

/**
 * Duration
 */
template<class T>
using Duration = ConcreteType<ParamDuration<T>>;

using Milliseconds = Duration<std::chrono::milliseconds>;
using Seconds = Duration<std::chrono::seconds>;

/**
 * Enum
 */
template<class T>
using Enum = ConcreteType<ParamEnum<T>>;

/**
 * Path
 */
using Path = ConcreteType<ParamPath>;

/**
 * Size
 */
using Size = ConcreteType<ParamSize>;

/**
 * Server
 */
using Server = ConcreteType<ParamServer>;

/**
 * Target
 */
using Target = ConcreteType<ParamTarget>;

/**
 * String
 */
using String = ConcreteType<ParamString>;

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
std::string ParamDuration<T>::to_string(const value_type& value) const
{
    std::stringstream ss;
    ss << value.count() << DurationSuffix::of(value);
    return ss.str();
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
json_t* ParamDuration<T>::to_json(const value_type& value) const
{
    return json_integer(std::chrono::duration_cast<std::chrono::milliseconds>(value).count());
}

template<class T>
bool ParamDuration<T>::from_json(const json_t* pJson,
                                 value_type* pValue,
                                 std::string* pMessage) const
{
    bool rv = false;

    if (json_is_integer(pJson))
    {
        std::chrono::milliseconds ms(json_integer_value(pJson));

        *pValue = std::chrono::duration_cast<value_type>(ms);
        rv = true;
    }
    else
    {
        *pMessage = "Expected a json integer, but got a json ";
        *pMessage += mxs::json_type_to_string(pJson);
        *pMessage += ".";
    }

    return rv;
}

template<class T>
ParamEnum<T>::ParamEnum(Specification* pSpecification,
                        const char* zName,
                        const char* zDescription,
                        Param::Modifiable modifiable,
                        Param::Kind kind,
                        const std::vector<std::pair<T, const char*>>& enumeration,
                        value_type default_value)
    : ConcreteParam<ParamEnum<T>, T>(pSpecification, zName, zDescription,
                                     modifiable, kind, MXS_MODULE_PARAM_ENUM, default_value)
    , m_enumeration(enumeration)
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
std::string ParamEnum<T>::to_string(value_type value) const
{
    auto it = std::find_if(m_enumeration.begin(), m_enumeration.end(),
                           [value](const std::pair<T, const char*>& entry) {
                               return entry.first == value;
                           });

    return it != m_enumeration.end() ? it->second : "unknown";
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
json_t* ParamEnum<T>::to_json(value_type value) const
{
    auto it = std::find_if(m_enumeration.begin(), m_enumeration.end(),
                           [value](const std::pair<T, const char*>& entry) {
                               return entry.first == value;
                           });

    return it != m_enumeration.end() ? json_string(it->second) : nullptr;
}

template<class T>
bool ParamEnum<T>::from_json(const json_t* pJson, value_type* pValue,
                             std::string* pMessage) const
{
    bool rv = false;

    if (json_is_string(pJson))
    {
        const char* z = json_string_value(pJson);

        rv = from_string(z, pValue, pMessage);
    }
    else
    {
        *pMessage = "Expected a json string, but got a json ";
        *pMessage += mxs::json_type_to_string(pJson);
        *pMessage += ".";
    }

    return rv;
}

template<class T>
void ParamEnum<T>::populate(MXS_MODULE_PARAM& param) const
{
    Param::populate(param);

    param.accepted_values = &m_enum_values[0];
}

template<class ParamType>
void Configuration::add_native(typename ParamType::value_type* pValue,
                               ParamType* pParam,
                               std::function<void(typename ParamType::value_type)> on_set)
{
    *pValue = pParam->default_value();
    m_natives.push_back(std::unique_ptr<Type>(new Native<ParamType>(this, pParam, pValue, on_set)));
}
}
}

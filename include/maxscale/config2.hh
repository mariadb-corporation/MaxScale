/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <maxbase/alloc.h>
#include <maxbase/assert.h>
#include <maxbase/atomic.hh>
#include <maxbase/host.hh>
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
        GLOBAL,
        SERVER
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
     * @return True, if `params` represent valid parameters - all mandatory are
     *         present, all present ones are of correct type - for this specification.
     */
    virtual bool validate(const mxs::ConfigParameters& params,
                          mxs::ConfigParameters* pUnrecognized = nullptr) const;

    /**
     *  Validate JSON
     *
     * @param pJson          JSON parameter object to validate
     * @param pUnrecognized  If non-null:
     *                       - Will contain on return object keys that were not used.
     *                       - An unrecognized parameter will not cause the configuring
     *                         to fail.
     *
     * @return True, if `pJson` represent valid JSON parameters - all mandatory are
     *         present, all present ones are of correct type - for this specification.
     */
    virtual bool validate(json_t* pJson, std::set<std::string>* pUnrecognized = nullptr) const;

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
     * @return Const iterator to first parameter.
     */
    const_iterator begin() const
    {
        return m_params.begin();
    }

    /**
     * @return Const iterator to one past last parameter.
     */
    const_iterator end() const
    {
        return m_params.end();
    }

    /**
     * @return Specification as a json array.
     */
    json_t* to_json() const;

protected:

    /**
     * Post validation step
     *
     * This can be overridden to check dependencies between parameters.
     *
     * @param params The set of validated parameters
     *
     * @return True, if the post validation check is successful.
     *
     * @note The default implementation always returns true
     */
    virtual bool post_validate(const mxs::ConfigParameters& params) const
    {
        return true;
    }

    /**
     * Post validation step
     *
     * This can be overridden to check dependencies between parameters.
     *
     * @param json The JSON parameter object to validate
     *
     * @return True, if the post validation check is successful.
     *
     * @note The default implementation always returns true
     */
    virtual bool post_validate(json_t* json) const
    {
        return true;
    }

private:
    friend Param;

    void insert(Param* pParam);
    void remove(Param* pParam);

    bool mandatory_params_defined(const std::set<std::string>& provided) const;

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
     * @return True if the parameter is deprecated
     */
    bool is_deprecated() const;

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
     * Validate JSON.
     *
     * @param value_as_json  The JSON to validate.
     *
     * @return True, if @c value_as_json can be converted into a value of this type.
     */
    virtual bool validate(json_t* value_as_json, std::string* pMessage) const = 0;

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

protected:
    Specification&              m_specification;
    const std::string           m_name;
    const std::string           m_description;
    const Modifiable            m_modifiable;
    const Kind                  m_kind;
    const mxs_module_param_type m_legacy_type;
};

/**
 * Deprecated parameter. Causes a warning to be logged if it is used.
 */
class ParamDeprecated : public Param
{
public:
    ParamDeprecated(Specification* pSpecification, const char* zName)
        : Param(pSpecification, zName, "This parameter is deprecated",
                AT_STARTUP, OPTIONAL, MXS_MODULE_PARAM_DEPRECATED)
    {
    }

    std::string type() const
    {
        return "deprecated";
    }

    std::string default_to_string() const override
    {
        return "deprecated";
    }

    bool validate(const std::string& value_as_string, std::string* pMessage) const
    {
        return true;
    }

    bool validate(json_t* value_as_json, std::string* pMessage) const
    {
        return true;
    }
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

    bool validate(json_t* value_as_json, std::string* pMessage) const override
    {
        value_type value;
        return static_cast<const ParamType*>(this)->from_json(value_as_json, &value, pMessage);
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
     * @param params The provided configuration parameters.
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

    /**
     * Get the parameter value from JSON
     *
     * @note Before calling this member function the JSON should have been
     *       validated by calling `Specification::validate(json)`.
     *
     * @param json The JSON object that defines the parameters.
     *
     * @return The value of this parameter if `json` contains a key with the name of this parameter. The
     *         default value if no key was found or the key was a JSON null.
     */
    value_type get(json_t* json) const
    {
        value_type rv {m_default_value};

        json_t* value = json_object_get(json, name().c_str());
        bool contains = value && !json_is_null(value);
        mxb_assert(!is_mandatory() || contains);

        if (contains)
        {
            const ParamType* pThis = static_cast<const ParamType*>(this);

            MXB_AT_DEBUG(bool valid = ) pThis->from_json(value, &rv);
            mxb_assert(valid);
        }

        return rv;
    }

    json_t* to_json() const
    {
        auto rv = Param::to_json();

        if (kind() == Kind::OPTIONAL)
        {
            auto self = static_cast<const ParamType*>(this);
            auto val = self->to_json(m_default_value);

            if (json_is_null(val))
            {
                // "empty" default values aren't added
                json_decref(val);
            }
            else
            {
                json_object_set_new(rv, "default_value", val);
            }
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
    json_t* to_json() const override;
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
    const std::vector<std::pair<T, const char*>>& values() const
    {
        return m_enumeration;
    }

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

    json_t* to_json() const override;

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
 * ParamEnumMask
 */
template<class T>
class ParamEnumMask : public ConcreteParam<ParamEnumMask<T>, uint32_t>
{
public:
    using value_type = uint32_t;

    ParamEnumMask(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  const std::vector<std::pair<T, const char*>>& enumeration,
                  Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamEnumMask(pSpecification, zName, zDescription, modifiable, Param::MANDATORY,
                        enumeration, value_type())
    {
    }

    ParamEnumMask(Specification* pSpecification,
                  const char* zName,
                  const char* zDescription,
                  const std::vector<std::pair<T, const char*>>& enumeration,
                  value_type default_value,
                  Param::Modifiable modifiable = Param::Modifiable::AT_STARTUP)
        : ParamEnumMask(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL,
                        enumeration, default_value)
    {
    }

    std::string type() const override;
    const std::vector<std::pair<T, const char*>>& values() const
    {
        return m_enumeration;
    }

    std::string to_string(value_type value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(value_type value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

    json_t* to_json() const override;

private:
    ParamEnumMask(Specification* pSpecification,
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
 * ParamHost
 */
class ParamHost : public ConcreteParam<ParamHost, maxbase::Host>
{
public:
    ParamHost(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamHost(pSpecification, zName, zDescription, modifiable, Param::MANDATORY, value_type())
    {
    }

    ParamHost(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              value_type default_value,
              Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamHost(pSpecification, zName, zDescription, modifiable, Param::OPTIONAL, default_value)
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
    ParamHost(Specification* pSpecification,
              const char* zName,
              const char* zDescription,
              Modifiable modifiable,
              Kind kind,
              const value_type& default_value)
        : ConcreteParam<ParamHost, maxbase::Host>(pSpecification, zName, zDescription,
                                                  modifiable, kind, MXS_MODULE_PARAM_STRING, default_value)
    {
    }
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
 * ParamRegex
 */

class RegexValue : public mxb::Regex
{
public:
    RegexValue() = default;
    RegexValue(const RegexValue&) = default;
    RegexValue& operator=(const RegexValue&) = default;

    RegexValue(const std::string& text,
               std::unique_ptr<pcre2_code> sCode,
               uint32_t ovec_size,
               uint32_t options)
        : mxb::Regex(text, sCode.release(), options)
        , ovec_size(ovec_size)
    {
    }

    bool operator==(const RegexValue& rhs) const
    {
        return this->pattern() == rhs.pattern()
               && this->ovec_size == rhs.ovec_size
               && this->options() == rhs.options()
               && (!this->valid() == !rhs.valid());     // Both have the same validity.
    }

    bool operator!=(const RegexValue& rhs) const
    {
        return !(*this == rhs);
    }

    uint32_t ovec_size {0};
};

class ParamRegex : public ConcreteParam<ParamRegex, RegexValue>
{
public:
    ParamRegex(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ConcreteParam<ParamRegex, RegexValue>(pSpecification, zName, zDescription,
                                                modifiable, Param::MANDATORY, MXS_MODULE_PARAM_REGEX,
                                                value_type())
    {
    }

    ParamRegex(Specification* pSpecification,
               const char* zName,
               const char* zDescription,
               const char* zRegex,
               Modifiable modifiable = Modifiable::AT_STARTUP)
        : ConcreteParam<ParamRegex, RegexValue>(pSpecification, zName, zDescription,
                                                modifiable, Param::OPTIONAL, MXS_MODULE_PARAM_REGEX,
                                                create_default(zRegex))
    {
    }

    uint32_t options() const
    {
        return m_options;
    }

    void set_options(uint32_t options)
    {
        m_options = options;
    }

    std::string type() const override;

    std::string to_string(const value_type& value) const;
    bool        from_string(const std::string& value, value_type* pValue,
                            std::string* pMessage = nullptr) const;

    json_t* to_json(const value_type& value) const;
    bool    from_json(const json_t* pJson, value_type* pValue,
                      std::string* pMessage = nullptr) const;

private:
    static RegexValue create_default(const char* zRegex);

    uint32_t m_options = 0;
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

    ParamServer(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Param::Kind kind,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ConcreteParam<ParamServer, SERVER*>(pSpecification, zName, zDescription,
                                              modifiable, kind, MXS_MODULE_PARAM_SERVER,
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
    enum Quotes
    {
        REQUIRED,   // The string *must* be surrounded by quotes.
        DESIRED,    // If there are no surrounding quotes, a warning is logged.
        IGNORED,    // The string may, but need not be surrounded by quotes. No warning.
    };

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, IGNORED, modifiable, Param::MANDATORY,
                      value_type())
    {
    }

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                Quotes quotes,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, quotes, modifiable, Param::MANDATORY, value_type())
    {
    }

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                value_type default_value,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, IGNORED, modifiable, Param::OPTIONAL,
                      default_value)
    {
    }

    ParamString(Specification* pSpecification,
                const char* zName,
                const char* zDescription,
                value_type default_value,
                Quotes quotes,
                Modifiable modifiable = Modifiable::AT_STARTUP)
        : ParamString(pSpecification, zName, zDescription, quotes, modifiable, Param::OPTIONAL, default_value)
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
                Quotes quotes,
                Modifiable modifiable,
                Kind kind,
                value_type default_value)
        : ConcreteParam<ParamString, std::string>(pSpecification, zName, zDescription,
                                                  modifiable, kind,
                                                  quotes != REQUIRED ?
                                                  MXS_MODULE_PARAM_STRING :
                                                  MXS_MODULE_PARAM_QUOTEDSTRING,
                                                  default_value)
        , m_quotes(quotes)
    {
    }

    Quotes m_quotes;
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

    Configuration(Configuration&& rhs);
    Configuration& operator=(Configuration&& rhs);

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
     * Configure this configuration
     *
     * @param params         The JSON parameter object that should be used, will be validated.
     * @param pUnrecognized  If non-null:
     *                       - Will contain on return object keys that were not used.
     *                       - An unrecognized parameter will not cause the configuring
     *                         to fail.
     *
     * @return True if could be configured.
     */
    virtual bool configure(json_t* json, std::set<std::string>* pUnrecognized = nullptr);

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
    const_iterator begin() const
    {
        return m_values.cbegin();
    }

    /**
     * @return Const iterator to one past last parameter.
     */
    const_iterator end() const
    {
        return m_values.cend();
    }

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
     * @return True, if everything is ok.
     *
     * @note The default implementation returns true.
     */
    virtual bool post_configure();

    /**
     * Add a native parameter value:
     * - will be configured at startup
     * - assumed not to be modified at runtime via admin interface
     *
     * @param pValue  Pointer to the parameter value.
     * @param pParam  Pointer to paramter describing value.
     * @param onSet   Optional functor to be called when value is set (at startup).
     */
    template<class ParamType, class ConcreteConfiguration>
    void add_native(typename ParamType::value_type ConcreteConfiguration::* pValue,
                    ParamType* pParam,
                    std::function<void(typename ParamType::value_type)> on_set = nullptr);

    template<class ParamType, class ConcreteConfiguration, class Container>
    void add_native(Container ConcreteConfiguration::* pContainer,
                    typename ParamType::value_type Container::* pValue,
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
     * Persist this value as a string. It will be written as
     *
     *    name=value
     *
     * where @c value will be formatted in the correct way.
     *
     * @return @c The formatted value.
     */
    std::string persist() const;

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

    friend Configuration;

    Configuration* m_pConfiguration;
    const Param*   m_pParam;
    std::string    m_name;
};

/**
 * Wrapper for native configuration value, not to be instantiated explicitly.
 */
template<class ParamType, class ConfigurationType>
class Native : public Type
{
public:
    using value_type = typename ParamType::value_type;

    Native(const Type& rhs) = delete;
    Native& operator=(const Native&) = delete;

    Native(ConfigurationType* pConfiguration,
           ParamType* pParam,
           typename ParamType::value_type ConfigurationType::*pValue,
           std::function<void(value_type)> on_set = nullptr)
        : Type(pConfiguration, pParam)
        , m_pValue(pValue)
        , m_on_set(on_set)
    {
    }

    // Native is move-only
    Native(Native&& rhs)
        : Type(rhs)
        , m_pValue(rhs.m_pValue)
        , m_on_set(rhs.m_on_set)
    {
        rhs.m_pValue = nullptr;
        rhs.m_on_set = nullptr;
    }

    Native& operator=(Native&& rhs)
    {
        if (this != &rhs)
        {
            Type::operator=(rhs);
            m_pValue = rhs.m_pValue;
            m_on_set = rhs.m_on_set;

            rhs.m_pValue = nullptr;
            rhs.m_on_set = nullptr;
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
        ConfigurationType* pConfiguration = static_cast<ConfigurationType*>(m_pConfiguration);

        return parameter().to_string(pConfiguration->*m_pValue);
    }

    json_t* to_json() const override final
    {
        ConfigurationType* pConfiguration = static_cast<ConfigurationType*>(m_pConfiguration);

        return parameter().to_json(pConfiguration->*m_pValue);
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
        return static_cast<ConfigurationType*>(m_pConfiguration)->*m_pValue;
    }

    bool set(const value_type& value)
    {
        bool rv = parameter().is_valid(value);

        if (rv)
        {
            static_cast<ConfigurationType*>(m_pConfiguration)->*m_pValue = value;

            if (m_on_set)
            {
                m_on_set(value);
            }
        }

        return rv;
    }

protected:
    typename ParamType::value_type ConfigurationType::* m_pValue;
    std::function<void(value_type)>                     m_on_set;
};

/**
 * Wrapper for contained native configuration value, not to be instantiated explicitly.
 */
template<class ParamType, class ConfigurationType, class Container>
class ContainedNative : public Type
{
public:
    using value_type = typename ParamType::value_type;

    ContainedNative(const Type& rhs) = delete;
    ContainedNative& operator=(const ContainedNative&) = delete;

    ContainedNative(ConfigurationType* pConfiguration,
                    ParamType* pParam,
                    Container ConfigurationType::* pContainer,
                    typename ParamType::value_type Container::*pValue,
                    std::function<void(value_type)> on_set = nullptr)
        : Type(pConfiguration, pParam)
        , m_pContainer(pContainer)
        , m_pValue(pValue)
        , m_on_set(on_set)
    {
    }

    // Native is move-only
    ContainedNative(ContainedNative&& rhs)
        : Type(rhs)
        , m_pContainer(rhs.m_pContainer)
        , m_pValue(rhs.m_pValue)
        , m_on_set(rhs.m_on_set)
    {
        rhs.m_pContainer = nullptr;
        rhs.m_pValue = nullptr;
        rhs.m_on_set = nullptr;
    }

    ContainedNative& operator=(ContainedNative&& rhs)
    {
        if (this != &rhs)
        {
            Type::operator=(rhs);
            m_pContainer = rhs.m_pContainer;
            m_pValue = rhs.m_pValue;
            m_on_set = rhs.m_on_set;

            rhs.m_pContainer = nullptr;
            rhs.m_pValue = nullptr;
            rhs.m_on_set = nullptr;
        }

        return *this;
    }

    ~ContainedNative() = default;

    const ParamType& parameter() const override final
    {
        return static_cast<const ParamType&>(*m_pParam);
    }

    std::string to_string() const override
    {
        ConfigurationType* pConfiguration = static_cast<ConfigurationType*>(m_pConfiguration);

        return parameter().to_string((pConfiguration->*m_pContainer).*m_pValue);
    }

    json_t* to_json() const override final
    {
        ConfigurationType* pConfiguration = static_cast<ConfigurationType*>(m_pConfiguration);

        return parameter().to_json((pConfiguration->*m_pContainer).*m_pValue);
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
        return (static_cast<ConfigurationType*>(m_pConfiguration)->*m_pContainer).*m_pValue;
    }

    bool set(const value_type& value)
    {
        bool rv = parameter().is_valid(value);

        if (rv)
        {
            (static_cast<ConfigurationType*>(m_pConfiguration)->*m_pContainer).*m_pValue = value;

            if (m_on_set)
            {
                m_on_set(value);
            }
        }

        return rv;
    }

protected:
    Container ConfigurationType::*              m_pContainer;
    typename ParamType::value_type Container::* m_pValue;
    std::function<void(value_type)>             m_on_set;
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
class Duration : public Type
{
public:
    using value_type = T;
    using ParamType = ParamDuration<T>;

    Duration(Duration&& rhs)
        : Type(std::forward<Duration &&>(rhs))
        , m_on_set(std::move(rhs.m_on_set))
    {
        m_value.store(rhs.m_value.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    Duration(Configuration* pConfiguration,
             const ParamType* pParam,
             std::function<void(value_type)> on_set = nullptr)
        : Type(pConfiguration, pParam)
        , m_on_set(on_set)
    {
        m_value.store(pParam->default_value().count(), std::memory_order_relaxed);
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
        return value_type(m_value.load(std::memory_order_relaxed));
    }

    bool set(const value_type& value)
    {
        bool rv = parameter().is_valid(value);

        if (rv)
        {
            m_value.store(value.count(), std::memory_order_relaxed);

            if (m_on_set)
            {
                m_on_set(value);
            }
        }

        return rv;
    }

    std::string to_string() const override
    {
        return parameter().to_string(get());
    }

    json_t* to_json() const override
    {
        return parameter().to_json(get());
    }

protected:
    std::atomic<int64_t>            m_value;
    std::function<void(value_type)> m_on_set;
};

using Milliseconds = Duration<std::chrono::milliseconds>;
using Seconds = Duration<std::chrono::seconds>;

/**
 * Enum
 */
template<class T>
using Enum = ConcreteType<ParamEnum<T>>;

/**
 * EnumMask
 */
template<class T>
using EnumMask = ConcreteType<ParamEnumMask<T>>;

/**
 * Host
 */
using Host = ConcreteType<ParamHost>;

/**
 * Path
 */
using Path = ConcreteType<ParamPath>;

/**
 * Regex
 */
using Regex = ConcreteType<ParamRegex>;

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
    ss << std::chrono::duration_cast<std::chrono::milliseconds>(value).count() << "ms";
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
json_t* ParamDuration<T>::to_json() const
{
    auto rv = ConcreteParam<ParamDuration<T>, T>::to_json();

    json_object_set_new(rv, "unit", json_string("ms"));

    return rv;
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
    else if (json_is_string(pJson))
    {
        return from_string(json_string_value(pJson), pValue, pMessage);
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
    return "enum";
}

template<class T>
json_t* ParamEnum<T>::to_json() const
{
    auto rv = ConcreteParam<ParamEnum<T>, T>::to_json();
    auto arr = json_array();

    for (const auto& a : m_enumeration)
    {
        json_array_append_new(arr, json_string(a.second));
    }

    json_object_set_new(rv, "enum_values", arr);

    return rv;
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
ParamEnumMask<T>::ParamEnumMask(Specification* pSpecification,
                                const char* zName,
                                const char* zDescription,
                                Param::Modifiable modifiable,
                                Param::Kind kind,
                                const std::vector<std::pair<T, const char*>>& enumeration,
                                value_type default_value)
    : ConcreteParam<ParamEnumMask<T>, uint32_t>(pSpecification, zName, zDescription,
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
std::string ParamEnumMask<T>::type() const
{
    return "enum_mask";
}

template<class T>
json_t* ParamEnumMask<T>::to_json() const
{
    auto rv = ConcreteParam<ParamEnumMask<T>, uint32_t>::to_json();
    auto arr = json_array();

    for (const auto& a : m_enumeration)
    {
        json_array_append_new(arr, json_string(a.second));
    }

    json_object_set_new(rv, "enum_values", arr);

    return rv;
}

template<class T>
std::string ParamEnumMask<T>::to_string(value_type value) const
{
    std::vector<std::string> values;

    for (const auto& entry : m_enumeration)
    {
        if (value & entry.first)
        {
            values.push_back(entry.second);
        }
    }

    return mxb::join(values, ",");
}

template<class T>
bool ParamEnumMask<T>::from_string(const std::string& value_as_string,
                                   value_type* pValue,
                                   std::string* pMessage) const
{
    bool rv = true;

    value_type value = 0;

    auto enum_values = mxb::strtok(value_as_string, ",");

    for (auto enum_value : enum_values)
    {
        mxb::trim(enum_value);

        auto it = std::find_if(m_enumeration.begin(), m_enumeration.end(),
                               [enum_value](const std::pair<T, const char*>& elem) {
                                   return enum_value == elem.second;
                               });

        if (it != m_enumeration.end())
        {
            value |= it->first;
        }
        else
        {
            rv = false;
            break;
        }
    }

    if (rv)
    {
        *pValue = value;
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
        *pMessage += ", valid values are a combination of: ";
        *pMessage += s;
        *pMessage += ".";
    }

    return rv;
}

template<class T>
json_t* ParamEnumMask<T>::to_json(value_type value) const
{
    return json_string(to_string(value).c_str());
}

template<class T>
bool ParamEnumMask<T>::from_json(const json_t* pJson, value_type* pValue,
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

template<class ParamType, class ConcreteConfiguration>
void Configuration::add_native(typename ParamType::value_type ConcreteConfiguration::* pValue,
                               ParamType* pParam,
                               std::function<void(typename ParamType::value_type)> on_set)
{
    ConcreteConfiguration* pThis = static_cast<ConcreteConfiguration*>(this);
    pThis->*pValue = pParam->default_value();
    m_natives.push_back(std::unique_ptr<Type>(new Native<ParamType,ConcreteConfiguration>(pThis,
                                                                                          pParam,
                                                                                          pValue,
                                                                                          on_set)));
}

template<class ParamType, class ConcreteConfiguration, class Container>
void
Configuration::add_native(Container ConcreteConfiguration::* pContainer,
                          typename ParamType::value_type Container::* pValue,
                          ParamType* pParam,
                          std::function<void(typename ParamType::value_type)> on_set)
{
    ConcreteConfiguration* pThis = static_cast<ConcreteConfiguration*>(this);
    (pThis->*pContainer).*pValue = pParam->default_value();

    auto* pType = new ContainedNative<ParamType,ConcreteConfiguration,Container>(pThis,
                                                                                 pParam,
                                                                                 pContainer,
                                                                                 pValue,
                                                                                 on_set);
    m_natives.push_back(std::unique_ptr<Type>(pType));
}

}
}

inline std::ostream& operator<<(std::ostream& out, const mxs::config::RegexValue& value)
{
    out << value.pattern();
    return out;
}

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxavro_internal.hh"
#include <jansson.h>
#include <string.h>
#include <maxbase/assert.h>
#include <maxscale/log.hh>

static const MAXAVRO_SCHEMA_FIELD types[MAXAVRO_TYPE_MAX] =
{
    {(char*)"int",    NULL, MAXAVRO_TYPE_INT    },
    {(char*)"long",   NULL, MAXAVRO_TYPE_LONG   },
    {(char*)"float",  NULL, MAXAVRO_TYPE_FLOAT  },
    {(char*)"double", NULL, MAXAVRO_TYPE_DOUBLE },
    {(char*)"bool",   NULL, MAXAVRO_TYPE_BOOL   },
    {(char*)"bytes",  NULL, MAXAVRO_TYPE_BYTES  },
    {(char*)"string", NULL, MAXAVRO_TYPE_STRING },
    {(char*)"enum",   NULL, MAXAVRO_TYPE_ENUM   },
    {(char*)"null",   NULL, MAXAVRO_TYPE_NULL   },
    {NULL,            NULL, MAXAVRO_TYPE_UNKNOWN}
};
/**
 * @brief Convert string to Avro value type
 *
 * @param type Value string
 * @return Avro value type
 */
enum maxavro_value_type string_to_type(const char* str)
{
    for (int i = 0; types[i].name; i++)
    {
        if (strcmp(str, types[i].name) == 0)
        {
            return types[i].type;
        }
    }
    return MAXAVRO_TYPE_UNKNOWN;
}

/**
 * @brief Convert Avro value type to string
 *
 * @param type Type of the value
 * @return String representation of the value
 */
const char* type_to_string(enum maxavro_value_type type)
{
    for (int i = 0; types[i].name; i++)
    {
        if (types[i].type == type)
        {
            return types[i].name;
        }
    }
    return "unknown type";
}

/**
 * @brief extract the type definition from a JSON schema
 * @param object JSON object containing the schema
 * @param field The associated field
 * @return Type of the field
 */
static enum maxavro_value_type unpack_to_type(json_t* object,
                                              MAXAVRO_SCHEMA_FIELD* field)
{
    enum maxavro_value_type rval = MAXAVRO_TYPE_UNKNOWN;
    json_t* type = NULL;

    if (json_is_array(object) && json_is_object(json_array_get(object, 0)))
    {
        json_incref(object);
        field->extra = object;
        return MAXAVRO_TYPE_UNION;
    }

    if (json_is_object(object))
    {
        json_t* tmp = NULL;
        json_unpack(object, "{s:o}", "type", &tmp);
        type = tmp;
    }

    if (json_is_array(object))
    {
        json_t* tmp = json_array_get(object, 0);
        type = tmp;
    }

    if (type && json_is_string(type))
    {
        const char* value = json_string_value(type);
        rval = string_to_type(value);

        if (rval == MAXAVRO_TYPE_ENUM)
        {
            json_t* tmp = NULL;
            json_unpack(object, "{s:o}", "symbols", &tmp);
            mxb_assert(json_is_array(tmp));
            json_incref(tmp);
            field->extra = tmp;
        }
    }

    return rval;
}

/**
 * @brief Create a new Avro schema from JSON
 * @param json JSON from which the schema is created from
 * @return New schema or NULL if an error occurred
 */
MAXAVRO_SCHEMA* maxavro_schema_alloc(const char* json)
{
    MAXAVRO_SCHEMA* rval = (MAXAVRO_SCHEMA*)malloc(sizeof(MAXAVRO_SCHEMA));

    if (rval)
    {
        bool error = false;
        json_error_t err;
        json_t* schema = json_loads(json, 0, &err);

        if (schema)
        {
            json_t* field_arr = NULL;

            if (json_unpack(schema, "{s:o}", "fields", &field_arr) == 0)
            {
                size_t arr_size = json_array_size(field_arr);
                rval->fields = (MAXAVRO_SCHEMA_FIELD*)malloc(sizeof(MAXAVRO_SCHEMA_FIELD) * arr_size);
                rval->num_fields = arr_size;

                for (int i = 0; i < (int)arr_size; i++)
                {
                    json_t* object = json_array_get(field_arr, i);
                    char* key;
                    json_t* value_obj;

                    if (object && json_unpack(object, "{s:s s:o}", "name", &key, "type", &value_obj) == 0)
                    {
                        rval->fields[i].name = strdup(key);
                        rval->fields[i].type = unpack_to_type(value_obj, &rval->fields[i]);
                    }
                    else
                    {
                        MXS_ERROR("Failed to unpack JSON Object \"name\": %s", json);
                        error = true;

                        for (int j = 0; j < i; j++)
                        {
                            MXS_FREE(rval->fields[j].name);
                        }
                        break;
                    }
                }
            }
            else
            {
                MXS_ERROR("Failed to unpack JSON Object \"fields\": %s", json);
                error = true;
            }

            json_decref(schema);
        }
        else
        {
            MXS_ERROR("Failed to read JSON schema: %s", json);
            error = true;
        }

        if (error)
        {
            MXS_FREE(rval);
            rval = NULL;
        }
    }
    else
    {
        MXS_ERROR("Memory allocation failed.");
    }

    return rval;
}

static void maxavro_schema_field_free(MAXAVRO_SCHEMA_FIELD* field)
{
    if (field)
    {
        MXS_FREE(field->name);
        if (field->type == MAXAVRO_TYPE_ENUM || field->type == MAXAVRO_TYPE_UNION)
        {
            json_decref((json_t*)field->extra);
        }
    }
}

/**
 * Free a MAXAVRO_SCHEMA object
 * @param schema Schema to free
 */
void maxavro_schema_free(MAXAVRO_SCHEMA* schema)
{
    if (schema)
    {
        for (unsigned int i = 0; i < schema->num_fields; i++)
        {
            maxavro_schema_field_free(&schema->fields[i]);
        }
        MXS_FREE(schema->fields);
        MXS_FREE(schema);
    }
}

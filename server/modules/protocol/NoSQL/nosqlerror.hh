// https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

#if !defined(NOSQL_ERROR)
#error nosqlerror.hh cannot be included without NOSQL_ERROR being defined.
#endif

// The "location" errors are not documented, but appears to be created
// more or less on the spot in the MongoDB code and used for fringe cases.

// *INDENT-OFF*
NOSQL_ERROR(OK,                                    0, "OK")
NOSQL_ERROR(INTERNAL_ERROR,                        1, "InternalError")
NOSQL_ERROR(BAD_VALUE,                             2, "BadValue")
NOSQL_ERROR(NO_SUCH_KEY,                           4, "NoSuchKey")
NOSQL_ERROR(FAILED_TO_PARSE,                       9, "FailedToParse")
NOSQL_ERROR(USER_NOT_FOUND,                       11, "UserNotFound")
NOSQL_ERROR(UNSUPPORTED_FORMAT,                   12, "UnsupportedFormat")
NOSQL_ERROR(UNAUTHORIZED,                         13, "Unauthorized")
NOSQL_ERROR(TYPE_MISMATCH,                        14, "TypeMismatch")
NOSQL_ERROR(INVALID_LENGTH,                       16, "InvalidLength")
NOSQL_ERROR(AUTHENTICATION_FAILED,                18, "AuthenticationFailed")
NOSQL_ERROR(NAMESPACE_NOT_FOUND,                  26, "NamespaceNotFound")
NOSQL_ERROR(ROLE_NOT_FOUND,                       31, "RoleNotFound")
NOSQL_ERROR(CONFLICTING_UPDATE_OPERATORS,         40, "ConflictingUpdateOperators")
NOSQL_ERROR(CURSOR_NOT_FOUND,                     43, "CursorNotFound")
NOSQL_ERROR(NAMESPACE_EXISTS,                     48, "NamespaceExists")
NOSQL_ERROR(DOLLAR_PREFIXED_FIELD_NAME,           52, "DollarPrefixedFieldName")
NOSQL_ERROR(EMPTY_FIELD_NAME,                     56, "EmptyFieldName")
NOSQL_ERROR(COMMAND_NOT_FOUND,                    59, "CommandNotFound")
NOSQL_ERROR(IMMUTABLE_FIELD,                      66, "ImmutableField")
NOSQL_ERROR(CANNOT_CREATE_INDEX,                  67, "CannotCreateIndex")
NOSQL_ERROR(INVALID_OPTIONS,                      72, "InvalidOptions")
NOSQL_ERROR(INVALID_NAMESPACE,                    73, "InvalidNamespace")
NOSQL_ERROR(NO_REPLICATION_ENABLED,               76, "NoReplicationEnabled")
NOSQL_ERROR(COMMAND_NOT_SUPPORTED,               115, "CommandNotSupported")
NOSQL_ERROR(COMMAND_FAILED,                      125, "CommandFailed")
NOSQL_ERROR(CLIENT_METADATA_MISSING_FIELD,       183, "ClientMetadataMissingField")
NOSQL_ERROR(CLIENT_METADATA_CANNOT_BE_MUTATED,   186, "ClientMetadataCannotBeMutated")
NOSQL_ERROR(MECHANISM_UNAVAILABLE,               334, "MechanismUnavailable")
NOSQL_ERROR(LOCATION10065,                     10065, "Location10065")
NOSQL_ERROR(DUPLICATE_KEY,                     11000, "DuplicateKey")
NOSQL_ERROR(LOCATION15974,                     15974, "Location15974")
NOSQL_ERROR(LOCATION15975,                     15975, "Location15975")
NOSQL_ERROR(LOCATION17419,                     17419, "Location17419")
NOSQL_ERROR(LOCATION17420,                     17420, "Location17420")
NOSQL_ERROR(LOCATION31032,                     31032, "Location31032")
NOSQL_ERROR(LOCATION31160,                     31160, "Location31160")
NOSQL_ERROR(LOCATION31174,                     31174, "Location31174")
NOSQL_ERROR(LOCATION31175,                     31175, "Location31175")
NOSQL_ERROR(LOCATION40352,                     40352, "Location40352")
NOSQL_ERROR(LOCATION40353,                     40353, "Location40353")
NOSQL_ERROR(LOCATION40414,                     40414, "Location40414")
NOSQL_ERROR(LOCATION51003,                     51003, "Location51003")
// *INDENT-ON*

// https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

#if !defined(NOSQL_ERROR)
#error nosqlerror.hh cannot be included without NOSQL_ERROR being defined.
#endif

// The "location" errors are not documented, but appears to be created
// more or less on the spot in the MongoDB code and used for fringe cases.

NOSQL_ERROR(OK,                          0, "OK")
NOSQL_ERROR(INTERNAL_ERROR,              1, "InternalError")
NOSQL_ERROR(BAD_VALUE,                   2, "BadValue")
NOSQL_ERROR(NO_SUCH_KEY,                 4, "NoSuchKey")
NOSQL_ERROR(FAILED_TO_PARSE,             9, "FailedToParse")
NOSQL_ERROR(UNAUTHORIZED,               13, "Unauthorized")
NOSQL_ERROR(TYPE_MISMATCH,              14, "TypeMismatch")
NOSQL_ERROR(INVALID_LENGTH,             16, "InvalidLength")
NOSQL_ERROR(NAMESPACE_NOT_FOUND,        26, "NamespaceNotFound")
NOSQL_ERROR(CURSOR_NOT_FOUND,           43, "CursorNotFound")
NOSQL_ERROR(NAMESPACE_EXISTS,           48, "NamespaceExists")
NOSQL_ERROR(DOLLAR_PREFIXED_FIELD_NAME, 52, "DollarPrefixedFieldName")
NOSQL_ERROR(COMMAND_NOT_FOUND,          59, "CommandNotFound")
NOSQL_ERROR(INVALID_OPTIONS,            72, "InvalidOptions")
NOSQL_ERROR(INVALID_NAMESPACE,          73, "InvalidNamespace")
NOSQL_ERROR(NO_REPLICATION_ENABLED,     76, "NoReplicationEnabled")
NOSQL_ERROR(COMMAND_FAILED,            125, "CommandFailed")
NOSQL_ERROR(LOCATION10065,           10065, "Location10065")
NOSQL_ERROR(DUPLICATE_KEY,           11000, "DuplicateKey")
NOSQL_ERROR(LOCATION15974,           15974, "Location15974")
NOSQL_ERROR(LOCATION15975,           15975, "Location15975")
NOSQL_ERROR(LOCATION40352,           40352, "Location40352")
NOSQL_ERROR(LOCATION40353,           40353, "Location40353")
NOSQL_ERROR(LOCATION40414,           40414, "Location40414")

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

#pragma once

#include "maxavro.hh"
#include <maxbase/alloc.h>

/**
 * Private header for maxavro
 */

/** Reading primitives */
bool  maxavro_read_integer(MAXAVRO_FILE* file, uint64_t* val);
char* maxavro_read_string(MAXAVRO_FILE* file, size_t* size);
bool  maxavro_skip_string(MAXAVRO_FILE* file);
bool  maxavro_read_float(MAXAVRO_FILE* file, float* dest);
bool  maxavro_read_double(MAXAVRO_FILE* file, double* dest);

/** Only used when opening the file */
bool maxavro_read_integer_from_file(MAXAVRO_FILE* file, uint64_t* val);

/** Reading complex types */
MAXAVRO_MAP* maxavro_read_map_from_file(MAXAVRO_FILE* file);
void         maxavro_map_free(MAXAVRO_MAP* value);

/**
 * The following functionality is not yet fully implemented
 */

/** Schema creation */
MAXAVRO_SCHEMA* maxavro_schema_alloc(const char* json);
void            maxavro_schema_free(MAXAVRO_SCHEMA* schema);

/** Data block generation */
MAXAVRO_DATABLOCK* maxavro_datablock_allocate(MAXAVRO_FILE* file, size_t buffersize);
void               maxavro_datablock_free(MAXAVRO_DATABLOCK* block);
bool               maxavro_datablock_finalize(MAXAVRO_DATABLOCK* block);

/** Adding values to a datablock. The caller must ensure that the inserted
 * values conform to the file schema and that the required amount of fields
 * is added before finalizing the block. */
bool maxavro_datablock_add_integer(MAXAVRO_DATABLOCK *file, uint64_t val);
bool maxavro_datablock_add_string(MAXAVRO_DATABLOCK *file, const char* str);
bool maxavro_datablock_add_float(MAXAVRO_DATABLOCK *file, float val);
bool maxavro_datablock_add_double(MAXAVRO_DATABLOCK *file, double val);

bool maxavro_read_datablock_start(MAXAVRO_FILE *file);
bool maxavro_verify_block(MAXAVRO_FILE *file);
const char* type_to_string(enum maxavro_value_type type);
enum maxavro_value_type string_to_type(const char *str);

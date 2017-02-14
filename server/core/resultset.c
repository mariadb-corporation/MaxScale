/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file resultset.c  - Implementation of a generic result set mechanism
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 17/02/15     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <string.h>
#include <ctype.h>
#include <maxscale/alloc.h>
#include <maxscale/resultset.h>
#include <maxscale/buffer.h>
#include <maxscale/dcb.h>


static int mysql_send_fieldcount(DCB *, int);
static int mysql_send_columndef(DCB *, const char *, int, int, uint8_t);
static int mysql_send_eof(DCB *, int);
static int mysql_send_row(DCB *, RESULT_ROW *, int);


/**
 * Create a generic result set
 *
 * @param func  Function to call for each row
 * @param data  Data to pass to the row retrieval function
 * @return      An empty resultset or NULL on error
 */
RESULTSET *
resultset_create(RESULT_ROW_CB func, void *data)
{
    RESULTSET *rval = (RESULTSET *)MXS_MALLOC(sizeof(RESULTSET));

    if (rval)
    {
        rval->n_cols = 0;
        rval->column = NULL;
        rval->userdata = data;
        rval->fetchrow = func;
    }
    return rval;
}

/**
 * Free a previously allocated resultset
 *
 * @param resultset     The result set to free
 */
void
resultset_free(RESULTSET *resultset)
{
    RESULT_COLUMN *col;

    if (resultset != NULL)
    {
        col = resultset->column;
        while (col)
        {
            RESULT_COLUMN *next;

            next = col->next;
            resultset_column_free(col);
            col = next;
        }
        MXS_FREE(resultset);
    }
}

/**
 * Add a new column to a result set. Columns are added to the right
 * of the result set, i.e. the existing order is maintained.
 *
 * @param set   The result set
 * @param name  The column name
 * @param len   The column length
 * @param type  The column type
 * @return      The numebr of columns added to the result set
 */
int
resultset_add_column(RESULTSET *set, const char *cname, int len, RESULT_COL_TYPE type)
{
    char *name = MXS_STRDUP(cname);
    RESULT_COLUMN *newcol = (RESULT_COLUMN *)MXS_MALLOC(sizeof(RESULT_COLUMN));

    if (!name || !newcol)
    {
        MXS_FREE(name);
        MXS_FREE(newcol);
        return 0;
    }

    newcol->name = name;
    newcol->type = type;
    newcol->len = len;
    newcol->next = NULL;

    if (set->column == NULL)
    {
        set->column = newcol;
    }
    else
    {
        RESULT_COLUMN *ptr = set->column;
        while (ptr->next)
        {
            ptr = ptr->next;
        }
        ptr->next = newcol;
    }
    set->n_cols++;
    return 1;
}

/**
 * Free a result set column
 *
 * @param col   Column to free
 */
void
resultset_column_free(RESULT_COLUMN *col)
{
    MXS_FREE(col->name);
    MXS_FREE(col);
}

/**
 * Create a blank row, a row with all values NULL, for a result
 * set.
 *
 * @param set   The result set the row will be part of
 * @return      The NULL result set row
 */
RESULT_ROW *
resultset_make_row(RESULTSET *set)
{
    RESULT_ROW *row;
    int i;

    if ((row = (RESULT_ROW *)MXS_MALLOC(sizeof(RESULT_ROW))) == NULL)
    {
        return NULL;
    }
    row->n_cols = set->n_cols;
    if ((row->cols = (char **)MXS_MALLOC(row->n_cols * sizeof(char *))) == NULL)
    {
        MXS_FREE(row);
        return NULL;
    }

    for (i = 0; i < set->n_cols; i++)
    {
        row->cols[i] = NULL;
    }
    return row;
}

/**
 * Free a result set row. If a column in the row has a non-null values
 * then the data is assumed to be a malloc'd pointer and will be free'd.
 * If any value is not a malloc'd pointer it should be removed before
 * making this call.
 *
 * @param row   The row to free
 */
void
resultset_free_row(RESULT_ROW *row)
{
    int i;

    for (i = 0; i < row->n_cols; i++)
    {
        if (row->cols[i])
        {
            MXS_FREE(row->cols[i]);
        }
    }
    MXS_FREE(row->cols);
    MXS_FREE(row);
}

/**
 * Add a value in a particular column of the row . The value is
 * a NULL terminated string and will be copied into malloc'd
 * storage by this routine.
 *
 * @param row   The row ro add the column into
 * @param col   The column number (0 to n_cols - 1)
 * @param value The column value, may be NULL
 * @return      The number of columns inserted
 */
int
resultset_row_set(RESULT_ROW *row, int col, const char *value)
{
    if (col < 0 || col >= row->n_cols)
    {
        return 0;
    }
    if (value)
    {
        if ((row->cols[col] = MXS_STRDUP(value)) == NULL)
        {
            return 0;
        }
        return 1;
    }
    else if (row->cols[col])
    {
        MXS_FREE(row->cols[col]);
    }
    row->cols[col] = NULL;
    return 1;
}

/**
 * Stream a result set using the MySQL protocol for encodign the result
 * set. Each row is retrieved by calling the function passed in the
 * argument list.
 *
 * @param set   The result set to stream
 * @param dcb   The connection to stream the result set to
 */
void
resultset_stream_mysql(RESULTSET *set, DCB *dcb)
{
    RESULT_COLUMN *col;
    RESULT_ROW *row;
    uint8_t seqno = 2;

    mysql_send_fieldcount(dcb, set->n_cols);

    col = set->column;
    while (col)
    {
        mysql_send_columndef(dcb, col->name, col->type, col->len, seqno++);
        col = col->next;
    }
    mysql_send_eof(dcb, seqno++);
    while ((row = (*set->fetchrow)(set, set->userdata)) != NULL)
    {
        mysql_send_row(dcb, row, seqno++);
        resultset_free_row(row);
    }
    mysql_send_eof(dcb, seqno);
}

/**
 * Send the field count packet in a response packet sequence.
 *
 * @param dcb           DCB of connection to send result set to
 * @param count         Number of columns in the result set
 * @return              Non-zero on success
 */
static int
mysql_send_fieldcount(DCB *dcb, int count)
{
    GWBUF *pkt;
    uint8_t *ptr;

    if ((pkt = gwbuf_alloc(5)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(pkt);
    *ptr++ = 0x01;                  // Payload length
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x01;                  // Sequence number in response
    *ptr++ = count;                 // Length of result string
    return dcb->func.write(dcb, pkt);
}


/**
 * Send the column definition packet in a response packet sequence.
 *
 * @param dcb           The DCB of the connection
 * @param name          Name of the column
 * @param type          Column type
 * @param len           Column length
 * @param seqno         Packet sequence number
 * @return              Non-zero on success
 */
static int
mysql_send_columndef(DCB *dcb, const char *name, int type, int len, uint8_t seqno)
{
    GWBUF *pkt;
    uint8_t *ptr;
    int plen;

    if ((pkt = gwbuf_alloc(26 + strlen(name))) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(pkt);
    plen = 22 + strlen(name);
    *ptr++ = plen & 0xff;
    *ptr++ = (plen >> 8) & 0xff;
    *ptr++ = (plen >> 16) & 0xff;
    *ptr++ = seqno;                         // Sequence number in response
    *ptr++ = 3;                             // Catalog is always def
    *ptr++ = 'd';
    *ptr++ = 'e';
    *ptr++ = 'f';
    *ptr++ = 0;                             // Schema name length
    *ptr++ = 0;                             // virtual table name length
    *ptr++ = 0;                             // Table name length
    *ptr++ = strlen(name);                  // Column name length;
    while (*name)
    {
        *ptr++ = *name++;                   // Copy the column name
    }
    *ptr++ = 0;                             // Orginal column name
    *ptr++ = 0x0c;                          // Length of next fields always 12
    *ptr++ = 0x3f;                          // Character set
    *ptr++ = 0;
    *ptr++ = len & 0xff;                    // Length of column
    *ptr++ = (len >> 8) & 0xff;
    *ptr++ = (len >> 16) & 0xff;
    *ptr++ = (len >> 24) & 0xff;
    *ptr++ = type;
    *ptr++ = 0x81;                          // Two bytes of flags
    if (type == 0xfd)
    {
        *ptr++ = 0x1f;
    }
    else
    {
        *ptr++ = 0x00;
    }
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;
    return dcb->func.write(dcb, pkt);
}


/**
 * Send an EOF packet in a response packet sequence.
 *
 * @param dcb           The client connection
 * @param seqno         The sequence number of the EOF packet
 * @return              Non-zero on success
 */
static int
mysql_send_eof(DCB *dcb, int seqno)
{
    GWBUF   *pkt;
    uint8_t *ptr;

    if ((pkt = gwbuf_alloc(9)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(pkt);
    *ptr++ = 0x05;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = seqno;                         // Sequence number in response
    *ptr++ = 0xfe;                          // Length of result string
    *ptr++ = 0x00;                          // No Errors
    *ptr++ = 0x00;
    *ptr++ = 0x02;                          // Autocommit enabled
    *ptr++ = 0x00;
    return dcb->func.write(dcb, pkt);
}



/**
 * Send a row packet in a response packet sequence.
 *
 * @param dcb           The client connection
 * @param row           The row to send
 * @param seqno         The sequence number of the EOF packet
 * @return              Non-zero on success
 */
static int
mysql_send_row(DCB *dcb, RESULT_ROW *row, int seqno)
{
    GWBUF *pkt;
    int i, len = 4;
    uint8_t *ptr;

    for (i = 0; i < row->n_cols; i++)
    {
        if (row->cols[i])
        {
            len += strlen(row->cols[i]);
        }
        len++;
    }

    if ((pkt = gwbuf_alloc(len)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(pkt);
    len -= 4;
    *ptr++ = len & 0xff;
    *ptr++ = (len >> 8) & 0xff;
    *ptr++ = (len >> 16) & 0xff;
    *ptr++ = seqno;
    for (i = 0; i < row->n_cols; i++)
    {
        if (row->cols[i])
        {
            len = strlen(row->cols[i]);
            *ptr++ = len;
            memcpy(ptr, row->cols[i], len);
            ptr += len;
        }
        else
        {
            *ptr++ = 0;     // NULL column
        }
    }

    return dcb->func.write(dcb, pkt);
}

/**
 * Return true if the string only contains numerics
 *
 * @param       value   String to test
 * @return      Non-zero if the string is made of of numeric values
 */
static int
value_is_numeric(const char *value)
{
    int rval = 0;

    if (*value)
    {
        rval = 1;
        while (*value)
        {
            if (!isdigit(*value))
            {
                return 0;
            }
            value++;
        }
    }

    return rval;
}

/**
 * Stream a result set encoding it as a JSON object
 * Each row is retrieved by calling the function passed in the
 * argument list.
 *
 * @param set   The result set to stream
 * @param dcb   The connection to stream the result set to
 */
void
resultset_stream_json(RESULTSET *set, DCB *dcb)
{
    RESULT_COLUMN *col;
    RESULT_ROW *row;
    int rowno = 0;

    dcb_printf(dcb, "[ ");
    while ((row = (*set->fetchrow)(set, set->userdata)) != NULL)
    {
        int i = 0;
        if (rowno++ > 0)
        {
            dcb_printf(dcb, ",\n");
        }
        dcb_printf(dcb, "{ ");
        col = set->column;
        while (col)
        {
            dcb_printf(dcb, "\"%s\" : ", col->name);
            if (row->cols[i])
            {
                if (value_is_numeric(row->cols[i]))
                {
                    dcb_printf(dcb, "%s", row->cols[i]);
                }
                else
                {
                    dcb_printf(dcb, "\"%s\"", row->cols[i]);
                }
            }
            else
            {
                dcb_printf(dcb, "null");
            }
            i++;
            col = col->next;
            if (col)
            {
                dcb_printf(dcb, ", ");
            }
        }
        resultset_free_row(row);
        dcb_printf(dcb, "}");
    }
    dcb_printf(dcb, "]\n");
}

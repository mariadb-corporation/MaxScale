<template>
    <div class="data-migration-page fill-height">
        <data-migration />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'

export default {
    name: 'data-migration-page',
    async created() {
        await QueryConn.dispatch('validateConns', {
            persistentConns: QueryConn.getters('getEtlConns'),
        })
    },
}
</script>

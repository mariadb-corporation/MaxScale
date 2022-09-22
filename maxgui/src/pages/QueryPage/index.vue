<template>
    <query-editor
        ref="queryEditor"
        class="query-editor-page fill-height"
        @leave-page="$router.push($event)"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'query-page',
    async beforeRouteLeave(to, from, next) {
        this.$refs.queryEditor.beforeRouteLeaveHandler(to, from, next)
    },
}
</script>

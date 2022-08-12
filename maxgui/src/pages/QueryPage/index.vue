<template>
    <query-editor class="query-editor-page" @leave-page="$router.push($event)" />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@queryEditor'
export default {
    name: 'query-page',
    components: { QueryEditor },
}
</script>
<style lang="scss" scoped>
.query-editor-page {
    // ignore root padding
    margin-left: -36px;
    margin-top: -24px;
    width: calc(100% + 72px);
    height: calc(100% + 48px);
}
</style>

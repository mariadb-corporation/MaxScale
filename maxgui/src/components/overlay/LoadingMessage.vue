<style lang="scss" scoped>
.welcome-txt {
    font-size: 30px;
}
</style>

<template>
    <div class="welcome-txt text-center color text-text-subtle">
        <div>{{ text }}</div>
        <div class="font-weight-medium">
            {{ $t('mariaDbMaxScale') }}
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'loading-message',
    props: {
        text: String,
    },
}
</script>

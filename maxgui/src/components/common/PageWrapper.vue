<template>
    <div class="wrapper-container fill-height">
        <div class="d-flex ml-n1">
            <portal-target name="page-header" />
            <v-spacer />
            <div class="d-flex flex-wrap ">
                <portal-target name="page-search" />
                <portal-target name="create-resource" />
            </div>
        </div>
        <slot></slot>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'page-wrapper',
}
</script>

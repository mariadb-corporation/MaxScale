<template>
    <v-sheet class="d-flex flex-column fill-height mt-12">
        <!-- TODO: Utilize v-stepper and its related components to navigate between etl stages -->
        <!-- TODO: Add the "Prerequisites" stage -->
        <etl-conn-form-ctr />
        <etl-src-tree />
    </v-sheet>
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
import EtlSrcTree from '@queryEditorSrc/components/EtlSrcTree.vue'
import EtlConnFormCtr from '@queryEditorSrc/components/EtlConnFormCtr.vue'

export default {
    name: 'data-migration-stage-ctr',
    components: {
        EtlSrcTree,
        EtlConnFormCtr,
    },
}
</script>

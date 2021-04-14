<template>
    <page-wrapper>
        <v-sheet class="py-6 fill-height">
            <page-header />
            <query-editor v-model="value" class="editor" :tableDist="distArr" />
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@/components/QueryEditor'
import PageHeader from './PageHeader'
export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        PageHeader,
    },
    data() {
        return {
            //TODO: Get database name, table name and its columns name from API
            dist: {}, // contains database name, table name and its columns
            value: '',
        }
    },
    computed: {
        distArr: function() {
            let result = []
            //TODO: Flatten dist
            return result
        },
    },
}
</script>

<style lang="scss" scoped>
.editor {
    border: 1px solid $table-border;
    width: 100%;
    height: 100%;
}
</style>

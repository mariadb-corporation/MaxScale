<template>
    <div class="fill-height mxs-color-helper border-top-table-border er-editor-ctr">
        <v-btn class="close absolute" small icon @click="close">
            <v-icon size="14" color="navigation"> $vuetify.icons.mxs_close</v-icon>
        </v-btn>
        <!-- TODO: Add DDL editor -->
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'

export default {
    name: 'entity-editor-ctr',
    computed: {
        activeErdConnId() {
            return this.$typy(QueryConn.getters('getActiveErdConn'), 'id').safeString
        },
    },
    async created() {
        await this.queryDdlEditorSuppData({ connId: this.activeErdConnId })
    },
    methods: {
        ...mapActions({ queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData' }),
        close() {
            ErdTaskTmp.update({
                where: ErdTask.getters('getActiveErdTaskId'),
                data: { graph_height_pct: 100, active_entity_id: '' },
            })
        },
    },
}
</script>

<style lang="scss" scoped>
.close {
    right: 12px;
    top: 8px;
}
</style>

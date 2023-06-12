<template>
    <div class="fill-height mxs-color-helper border-top-table-border er-editor-ctr">
        <mxs-ddl-editor
            v-if="stagingData"
            v-model="stagingData"
            :dim="dim"
            :data="data"
            :mode="editorMode"
            :onExecute="onExecute"
        >
            <template v-slot:toolbar-append>
                <v-spacer />
                <mxs-tooltip-btn btnClass="toolbar-square-btn" text color="error" @click="close">
                    <template v-slot:btn-content>
                        <v-icon size="12" color="error"> $vuetify.icons.mxs_close</v-icon>
                    </template>
                    {{ $mxs_t('close') }}
                </mxs-tooltip-btn>
            </template>
        </mxs-ddl-editor>
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
import { mapState, mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'

export default {
    name: 'entity-editor-ctr',
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            stagingData: null,
        }
    },
    computed: {
        ...mapState({
            DDL_EDITOR_MODES: state => state.mxsWorkspace.config.DDL_EDITOR_MODES,
        }),
        activeErdConnId() {
            return this.$typy(QueryConn.getters('getActiveErdConn'), 'id').safeString
        },
        activeErdTaskId() {
            return ErdTask.getters('getActiveErdTaskId')
        },
        entities() {
            return this.$typy(ErdTask.getters('getActiveErdTask'), 'data.nodes').safeArray
        },
        activeEntityId() {
            return ErdTask.getters('getActiveEntityId')
        },
        activeEntity() {
            return this.entities.find(item => item.id === this.activeEntityId)
        },
        data() {
            return this.$typy(this.activeEntity, 'data').safeObjectOrEmpty
        },
        editorMode() {
            return this.DDL_EDITOR_MODES.ALTER
        },
    },
    async created() {
        await this.queryDdlEditorSuppData({ connId: this.activeErdConnId })
    },
    activated() {
        this.watch_activeEntityId()
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
    },
    methods: {
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
            confirmAlter: 'mxsWorkspace/confirmAlter',
        }),
        //Watcher to work with multiple worksheets which are kept alive
        watch_activeEntityId() {
            this.unwatch_activeEntityId = this.$watch(
                'activeEntityId',
                (v, oV) => {
                    if ((v && this.$typy(this.stagingData).isNull) || (v && oV))
                        this.stagingData = this.$helpers.lodash.cloneDeep(this.data)
                },
                { immediate: true }
            )
        },
        close() {
            ErdTaskTmp.update({
                where: this.activeErdTaskId,
                data: { graph_height_pct: 100, active_entity_id: '' },
            })
        },
        async onExecute() {
            await this.confirmAlter({
                connId: this.activeErdConnId,
                schema: this.data.options.schema,
                name: this.data.options.name,
                successCb: () => {
                    const {
                        immutableUpdate,
                        lodash: { cloneDeep },
                    } = this.$helpers
                    const activeEntityId = this.activeEntityId
                    const data = cloneDeep(this.stagingData)
                    ErdTask.update({
                        where: this.activeErdTaskId,
                        data(task) {
                            const idx = task.data.nodes.findIndex(n => n.id === activeEntityId)
                            task.data.nodes = immutableUpdate(task.data.nodes, {
                                [idx]: { data: { $set: data } },
                            })
                        },
                    })
                },
            })
        },
    },
}
</script>

<template>
    <div class="fill-height mxs-color-helper border-top-table-border er-editor-ctr">
        <mxs-ddl-editor
            v-if="stagingData"
            v-model="stagingData"
            :dim="dim"
            :initialData="initialData"
            :isCreating="isCreating"
            :schemas="stagingSchemas"
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
import { mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import { EventBus } from '@wkeComps/EventBus'

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
        isCreating() {
            return ErdTask.getters('isNewEntity')
        },
        activeErdConnId() {
            return this.$typy(QueryConn.getters('activeErdConn'), 'id').safeString
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        initialNodes() {
            return ErdTask.getters('initialNodes')
        },
        stagingNodes() {
            return ErdTask.getters('stagingNodes')
        },
        activeEntityId() {
            return ErdTask.getters('activeEntityId')
        },
        initialActiveNode() {
            return this.initialNodes.find(item => item.id === this.activeEntityId)
        },
        stagingActiveNode() {
            return this.stagingNodes.find(item => item.id === this.activeEntityId)
        },
        // persisted data
        initialData() {
            return this.$typy(this.initialActiveNode, 'data').safeObjectOrEmpty
        },
        stagingInitialData() {
            return this.$typy(this.stagingActiveNode, 'data').safeObjectOrEmpty
        },
        stagingSchemas() {
            return ErdTask.getters('stagingSchemas')
        },
        eventBus() {
            return EventBus
        },
    },
    async created() {
        await this.queryDdlEditorSuppData({
            connId: this.activeErdConnId,
            config: Worksheet.getters('activeRequestConfig'),
        })
    },
    activated() {
        this.watch_activeEntityId()
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
        this.$typy(this.unwatch_stagingData).safeFunction()
    },
    methods: {
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
            exeDdlScript: 'mxsWorkspace/exeDdlScript',
        }),
        //Watcher to work with multiple worksheets which are kept alive
        watch_activeEntityId() {
            this.unwatch_activeEntityId = this.$watch(
                'activeEntityId',
                v => {
                    if (v) {
                        this.stagingData = this.$helpers.lodash.cloneDeep(this.stagingInitialData)
                        this.watch_stagingData()
                    }
                },
                { immediate: true }
            )
        },
        watch_stagingData() {
            this.unwatch_stagingData = this.$watch(
                'stagingData',
                data => {
                    const { immutableUpdate } = this.$helpers
                    const id = this.activeEntityId

                    let nodes = this.stagingNodes

                    const idx = nodes.findIndex(n => n.id === id)
                    nodes = immutableUpdate(nodes, { [idx]: { data: { $set: data } } })

                    ErdTaskTmp.update({ where: this.activeTaskId, data: { nodes } })
                    this.eventBus.$emit('entity-editor-ctr-update-node-data', { id, data })
                },
                { deep: true }
            )
        },
        close() {
            ErdTaskTmp.update({
                where: this.activeTaskId,
                data: { graph_height_pct: 100, active_entity_id: '' },
            })
        },
        async onExecute() {
            const { options } = this.isCreating ? this.stagingData : this.initialData
            const { schema, name } = options
            await this.exeDdlScript({
                connId: this.activeErdConnId,
                schema,
                name,
                successCb: async () => {
                    ErdTask.update({ where: this.activeTaskId, data: { nodes: this.stagingNodes } })
                    this.eventBus.$emit('entity-editor-ctr-update-node-data', {
                        id: this.activeEntityId,
                        data: this.stagingData,
                    })
                },
            })
        },
    },
}
</script>

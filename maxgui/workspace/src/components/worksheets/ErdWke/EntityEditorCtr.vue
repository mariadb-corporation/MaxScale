<template>
    <mxs-ddl-editor
        v-if="stagingData"
        v-model="stagingData"
        class="fill-height mxs-color-helper border-top-table-border er-editor-ctr"
        :activeSpec.sync="activeSpec"
        :dim="dim"
        :initialData="{}"
        isCreating
        :schemas="schemas"
        :lookupTables="nodes.reduce((map, n) => ((map[n.id] = n.data), map), {})"
        :connData="{ id: activeErdConnId, config: activeRequestConfig }"
        :onExecute="onExecute"
        v-on="$listeners"
    >
        <template v-slot:toolbar-append="{ formRef }">
            <v-spacer />
            <mxs-tooltip-btn
                btnClass="toolbar-square-btn"
                text
                color="error"
                @click="close(formRef)"
            >
                <template v-slot:btn-content>
                    <v-icon size="12" color="error"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('close') }}
            </mxs-tooltip-btn>
        </template>
    </mxs-ddl-editor>
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
import { mapActions, mapMutations } from 'vuex'
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
        activeErdConnId() {
            return this.$typy(QueryConn.getters('activeErdConn'), 'id').safeString
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        nodes() {
            return ErdTask.getters('nodes')
        },
        activeEntityId() {
            return ErdTask.getters('activeEntityId')
        },
        stagingActiveNode() {
            return this.nodes.find(item => item.id === this.activeEntityId)
        },
        stagingInitialData() {
            return this.$typy(this.stagingActiveNode, 'data').safeObjectOrEmpty
        },
        schemas() {
            return ErdTask.getters('schemas')
        },
        eventBus() {
            return EventBus
        },
        activeRequestConfig() {
            return Worksheet.getters('activeRequestConfig')
        },
        activeSpec: {
            get() {
                return ErdTask.getters('activeSpec')
            },
            set(v) {
                ErdTaskTmp.update({
                    where: this.activeTaskId,
                    data: { active_spec: v },
                })
            },
        },
    },
    activated() {
        this.watch()
    },
    deactivated() {
        this.unwatch()
    },
    beforeDestroy() {
        this.unwatch()
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
            exeDdlScript: 'mxsWorkspace/exeDdlScript',
        }),
        watch() {
            this.watch_activeEntityId()
            this.watch_activeErdConnId()
        },
        unwatch() {
            this.$typy(this.unwatch_activeEntityId).safeFunction()
            this.$typy(this.unwatch_stagingData).safeFunction()
            this.$typy(this.unwatch_activeErdConnId).safeFunction()
        },
        //Watcher to work with multiple worksheets which are kept alive
        watch_activeEntityId() {
            this.unwatch_activeEntityId = this.$watch(
                'activeEntityId',
                v => {
                    if (v) {
                        this.$typy(this.unwatch_stagingData).safeFunction()
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
                    let nodes = this.nodes

                    const idx = nodes.findIndex(n => n.id === id)
                    nodes = immutableUpdate(nodes, { [idx]: { data: { $set: data } } })

                    ErdTask.update({ where: this.activeTaskId, data: { nodes } })
                    ErdTask.dispatch('updateNodesHistory', nodes)
                    // Emit the event to update the node in the diagram
                    this.eventBus.$emit('entity-editor-ctr-update-node-data', { id, data })
                },
                { deep: true }
            )
        },
        watch_activeErdConnId() {
            this.unwatch_activeErdConnId = this.$watch(
                'activeErdConnId',
                async v => {
                    if (v)
                        await this.queryDdlEditorSuppData({
                            connId: this.activeErdConnId,
                            config: this.activeRequestConfig,
                        })
                },
                { immediate: true }
            )
        },
        close(formRef) {
            if (formRef.validate())
                ErdTaskTmp.update({
                    where: this.activeTaskId,
                    data: { graph_height_pct: 100, active_entity_id: '' },
                })
            else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredInputs')],
                    type: 'error',
                })
        },
        async onExecute() {
            const { options } = this.stagingData
            const { schema, name } = options
            await this.exeDdlScript({
                connId: this.activeErdConnId,
                isCreating: true,
                schema,
                name,
            })
        },
    },
}
</script>

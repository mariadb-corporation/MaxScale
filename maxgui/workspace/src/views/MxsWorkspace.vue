<template>
    <div
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="mxs-workspace fill-height"
        @shortkey="eventBus.$emit('shortkey', $event.srcKey)"
    >
        <div
            class="fill-height d-flex flex-column"
            :class="{ 'mxs-workspace--fullscreen': is_fullscreen }"
        >
            <div v-if="$slots['mxs-workspace-top']" ref="workspaceTopSlot">
                <slot name="mxs-workspace-top" />
            </div>
            <v-progress-linear v-if="is_validating_conn" indeterminate color="primary" />
            <template v-else>
                <wke-nav-ctr
                    v-if="!hidden_comp.includes('wke-nav-ctr')"
                    :height="wkeNavCtrHeight"
                />
                <keep-alive v-for="wke in allWorksheets" :key="wke.id" max="15">
                    <template v-if="activeWkeId === wke.id && ctrDim.height">
                        <!-- query-editor has query-editor-conn-manager slot -->
                        <query-editor v-if="isQueryEditorWke(wke)" ref="wke" :ctrDim="ctrDim">
                            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                        </query-editor>
                        <data-migration v-else-if="isEtlWke(wke)" :ctrDim="ctrDim" />
                    </template>
                    <!-- TODO: Show blank worksheet: EtlTasks and card nav  -->
                </keep-alive>
            </template>
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
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import Worksheet from '@wsModels/Worksheet'
import '@wsSrc/styles/workspace.scss'
import WkeNavCtr from '@wsComps/WkeNavCtr.vue'
import QueryEditor from '@wkeComps/QueryEditor'
import DataMigration from '@wkeComps/DataMigration'
import { EventBus } from '@wkeComps/QueryEditor/EventBus'

export default {
    name: 'mxs-workspace',
    components: {
        WkeNavCtr,
        QueryEditor,
        DataMigration,
    },
    data() {
        return {
            dim: {},
            workspaceTopSlotHeight: 0,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.prefAndStorage.is_fullscreen,
            is_validating_conn: state => state.queryConnsMem.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.mxsWorkspace.config.QUERY_SHORTCUT_KEYS,
            hidden_comp: state => state.mxsWorkspace.hidden_comp,
        }),
        allWorksheets() {
            return Worksheet.all()
        },
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        wkeNavCtrHeight() {
            return this.hidden_comp.includes('wke-nav-ctr') ? 0 : 32
        },
        ctrDim() {
            return {
                width: this.dim.width,
                height: this.dim.height - this.wkeNavCtrHeight - this.workspaceTopSlotHeight,
            }
        },
        eventBus() {
            return EventBus
        },
    },
    watch: {
        is_fullscreen(v) {
            if (v)
                this.dim = {
                    width: document.body.clientWidth,
                    height: document.body.clientHeight,
                }
            else this.$helpers.doubleRAF(() => this.setDim())
        },
    },
    created() {
        this.handleAutoClearQueryHistory()
    },
    mounted() {
        this.$nextTick(() => this.setDim(), this.setWorkspaceTopSlotHeight())
    },

    methods: {
        ...mapActions({
            handleAutoClearQueryHistory: 'prefAndStorage/handleAutoClearQueryHistory',
        }),
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.getBoundingClientRect()
            this.dim = { width, height }
        },
        setWorkspaceTopSlotHeight() {
            if (this.$refs.workspaceTopSlot) {
                const { height } = this.$refs.workspaceTopSlot.getBoundingClientRect()
                this.workspaceTopSlotHeight = height
            }
        },
        isQueryEditorWke(wke) {
            return Boolean(this.$typy(wke, 'active_query_tab_id').safeString)
        },
        isEtlWke(wke) {
            return Boolean(this.$typy(wke, 'active_etl_task_id').safeString)
        },
    },
}
</script>
<style lang="scss" scoped>
.mxs-workspace {
    &--fullscreen {
        background: white;
        z-index: 7;
        position: fixed;
        top: 0px;
        right: 0px;
        bottom: 0px;
        left: 0px;
    }
}
</style>

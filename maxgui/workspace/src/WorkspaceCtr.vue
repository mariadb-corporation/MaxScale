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
            <v-progress-linear v-if="is_validating_conn" indeterminate color="primary" />
            <template v-else>
                <wke-nav-ctr
                    v-if="!hidden_comp.includes('wke-nav-ctr')"
                    :height="wkeNavCtrHeight"
                />
                <template v-if="ctrDim.height">
                    <blank-wke v-if="isBlankWke(activeWke)" :key="activeWkeId" :ctrDim="ctrDim" />
                    <!-- Keep alive worksheets -->
                    <keep-alive v-for="wke in keptAliveWorksheets" :key="wke.id" max="15">
                        <template v-if="activeWkeId === wke.id">
                            <!-- query-editor has query-tab-nav-toolbar-right-slot used by SkySQL -->
                            <query-editor v-if="isQueryEditorWke(wke)" :ctrDim="ctrDim">
                                <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                            </query-editor>
                            <data-migration v-else :ctrDim="ctrDim" :taskId="wke.etl_task_id" />
                        </template>
                    </keep-alive>
                </template>
            </template>
            <migr-dlg />
            <reconn-dlg-ctr />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import BlankWke from '@wkeComps/BlankWke'
import QueryEditor from '@wkeComps/QueryEditor'
import DataMigration from '@wkeComps/DataMigration'
import MigrDlg from '@wkeComps/BlankWke/MigrDlg'
import ReconnDlgCtr from '@wsComps/ReconnDlgCtr.vue'
import { EventBus } from '@wkeComps/QueryEditor/EventBus'

export default {
    name: 'workspace-ctr',
    components: {
        WkeNavCtr,
        BlankWke,
        MigrDlg,
        QueryEditor,
        DataMigration,
        ReconnDlgCtr,
    },
    data() {
        return {
            dim: {},
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.prefAndStorage.is_fullscreen,
            is_validating_conn: state => state.queryConnsMem.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.mxsWorkspace.config.QUERY_SHORTCUT_KEYS,
            hidden_comp: state => state.mxsWorkspace.hidden_comp,
        }),
        keptAliveWorksheets() {
            return Worksheet.query()
                .where(wke => this.isQueryEditorWke(wke) || this.isEtlWke(wke))
                .get()
        },
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        activeWke() {
            return Worksheet.getters('getActiveWke')
        },
        wkeNavCtrHeight() {
            return this.hidden_comp.includes('wke-nav-ctr') ? 0 : 32
        },
        ctrDim() {
            return {
                width: this.dim.width,
                height: this.dim.height - this.wkeNavCtrHeight,
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
        this.$nextTick(() => this.setDim())
    },
    methods: {
        ...mapActions({
            handleAutoClearQueryHistory: 'prefAndStorage/handleAutoClearQueryHistory',
        }),
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.getBoundingClientRect()
            this.dim = { width, height }
        },
        isQueryEditorWke(wke) {
            return Boolean(this.$typy(wke, 'active_query_tab_id').safeString)
        },
        isEtlWke(wke) {
            return Boolean(this.$typy(wke, 'etl_task_id').safeString)
        },
        isBlankWke(wke) {
            return !this.isQueryEditorWke(wke) && !this.isEtlWke(wke)
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

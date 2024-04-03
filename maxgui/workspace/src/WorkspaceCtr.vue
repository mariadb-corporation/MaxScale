<template>
    <div
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="mxs-workspace fill-height"
        @shortkey="eventBus.$emit('workspace-shortkey', $event.srcKey)"
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
                    <!-- blank-wke has blank-worksheet-task-cards-bottom slot used by SkySQL -->
                    <blank-wke
                        v-if="isBlankWke(activeWke)"
                        :key="activeWkeId"
                        :ctrDim="ctrDim"
                        :cards="blankWkeCards"
                    >
                        <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                    </blank-wke>
                    <!-- Keep alive worksheets -->
                    <keep-alive v-for="wke in keptAliveWorksheets" :key="wke.id" max="15">
                        <template v-if="activeWkeId === wke.id">
                            <!-- query-editor has query-tab-nav-toolbar-right slot used by SkySQL -->
                            <query-editor
                                v-if="isQueryEditorWke(wke)"
                                :ctrDim="ctrDim"
                                :queryEditorId="wke.query_editor_id"
                            >
                                <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                            </query-editor>
                            <erd-wke v-else-if="isErdWke(wke)" :ctrDim="ctrDim" :wke="wke" />
                            <data-migration v-else :ctrDim="ctrDim" :taskId="wke.etl_task_id" />
                        </template>
                    </keep-alive>
                </template>
            </template>
            <execute-sql-dialog />
            <confirm-dlg />
            <migr-delete-dlg />
            <gen-erd-dlg />
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapMutations } from 'vuex'
import Worksheet from '@wsModels/Worksheet'
import WkeNavCtr from '@wsComps/WkeNavCtr.vue'
import BlankWke from '@wkeComps/BlankWke'
import DataMigration from '@wkeComps/DataMigration'
import ErdWke from '@wkeComps/ErdWke'
import { EventBus } from '@wkeComps/EventBus'
import QueryEditor from '@wkeComps/QueryEditor'
import MigrDeleteDlg from '@wkeComps/DataMigration/MigrDeleteDlg.vue'
import ExecuteSqlDialog from '@wsComps/ExecuteSqlDialog.vue'
import ConfirmDlg from '@wsComps/ConfirmDlg.vue'
import GenErdDlg from '@wsComps/GenErdDlg.vue'
import ReconnDlgCtr from '@wsComps/ReconnDlgCtr.vue'
import { QUERY_CONN_BINDING_TYPES, QUERY_SHORTCUT_KEYS, MIGR_DLG_TYPES } from '@wsSrc/constants'
import '@wsSrc/styles/workspace.scss'

export default {
    name: 'workspace-ctr',
    components: {
        WkeNavCtr,
        BlankWke,
        DataMigration,
        ErdWke,
        QueryEditor,
        MigrDeleteDlg,
        ExecuteSqlDialog,
        ConfirmDlg,
        GenErdDlg,
        ReconnDlgCtr,
    },
    props: {
        disableRunQueries: { type: Boolean, default: false },
        disableDataMigration: { type: Boolean, default: false },
        runQueriesSubtitle: { type: String, default: '' },
        dataMigrationSubtitle: { type: String, default: '' },
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
            hidden_comp: state => state.mxsWorkspace.hidden_comp,
        }),
        keptAliveWorksheets() {
            return Worksheet.query()
                .where(
                    wke => this.isQueryEditorWke(wke) || this.isEtlWke(wke) || this.isErdWke(wke)
                )
                .get()
        },
        activeWkeId() {
            return Worksheet.getters('activeId')
        },
        activeWke() {
            return Worksheet.getters('activeRecord')
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
        blankWkeCards() {
            return [
                {
                    title: this.$mxs_t('runQueries'),
                    subtitle: this.runQueriesSubtitle,
                    icon: '$vuetify.icons.mxs_workspace',
                    iconSize: 26,
                    disabled: this.disableRunQueries,
                    click: () =>
                        this.SET_CONN_DLG({
                            is_opened: true,
                            type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
                        }),
                },
                {
                    title: this.$mxs_t('dataMigration'),
                    subtitle: this.dataMigrationSubtitle,
                    icon: '$vuetify.icons.mxs_dataMigration',
                    iconSize: 32,
                    disabled: this.disableDataMigration,
                    click: () =>
                        this.SET_MIGR_DLG({ type: MIGR_DLG_TYPES.CREATE, is_opened: true }),
                },
                {
                    title: this.$mxs_t('createAnErd'),
                    icon: '$vuetify.icons.mxs_erd',
                    iconSize: 32,
                    click: () =>
                        this.SET_CONN_DLG({
                            is_opened: true,
                            type: QUERY_CONN_BINDING_TYPES.ERD,
                        }),
                },
            ]
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
        this.QUERY_SHORTCUT_KEYS = QUERY_SHORTCUT_KEYS
        this.handleAutoClearQueryHistory()
    },
    mounted() {
        this.$nextTick(() => this.setDim())
    },
    methods: {
        ...mapMutations({
            SET_CONN_DLG: 'mxsWorkspace/SET_CONN_DLG',
            SET_MIGR_DLG: 'mxsWorkspace/SET_MIGR_DLG',
        }),
        ...mapActions({
            handleAutoClearQueryHistory: 'prefAndStorage/handleAutoClearQueryHistory',
        }),
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.getBoundingClientRect()
            this.dim = { width, height }
        },
        isErdWke(wke) {
            return Boolean(this.$typy(wke, 'erd_task_id').safeString)
        },
        isEtlWke(wke) {
            return Boolean(this.$typy(wke, 'etl_task_id').safeString)
        },
        isQueryEditorWke(wke) {
            return Boolean(this.$typy(wke, 'query_editor_id').safeString)
        },
        isBlankWke(wke) {
            return !this.isQueryEditorWke(wke) && !this.isEtlWke(wke) && !this.isErdWke(wke)
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

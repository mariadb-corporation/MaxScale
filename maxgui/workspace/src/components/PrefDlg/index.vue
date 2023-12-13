<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_tc('pref')"
        :lazyValidation="false"
        minBodyWidth="800px"
        :hasChanged="hasChanged"
        bodyCtrClass="px-0 pb-4"
        formClass="px-12 py-0"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:form-body>
            <v-tabs
                v-model="activePrefType"
                vertical
                class="v-tabs--mariadb v-tabs--mariadb--vert fill-height"
                hide-slider
                eager
            >
                <v-tab
                    v-for="(item, type) in prefFieldMap"
                    :key="type"
                    :href="`#${type}`"
                    class="justify-space-between align-center"
                >
                    <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                        {{ type }}
                    </div>
                </v-tab>
                <v-tabs-items
                    v-if="!$typy(preferences).isEmptyObject"
                    v-model="activePrefType"
                    class="fill-height"
                >
                    <v-tab-item
                        v-for="(item, type) in prefFieldMap"
                        :key="type"
                        :value="type"
                        class="fill-height"
                    >
                        <!--isOpened is added as key to trigger rerender when dialog is opened,
                        otherwise row-limit-ctr input will be empty -->
                        <div :key="isOpened" class="pl-4 pr-2 overflow-y-auto pref-fields-ctr">
                            <template v-for="(fields, type) in item">
                                <template v-for="field in fields">
                                    <pref-field
                                        v-if="!$typy(preferences[field.id]).isNull"
                                        :key="field.id"
                                        v-model="preferences[field.id]"
                                        :type="type"
                                        :field="field"
                                        @tooltip="tooltip = $event"
                                    />
                                </template>
                            </template>
                        </div>
                        <div v-if="type === PREF_TYPES.CONN" class="pl-4 pt-2">
                            <small>{{ $mxs_t('info.timeoutVariables') }}</small>
                        </div>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
            <v-tooltip
                v-if="$typy(tooltip, 'activator').safeString"
                :value="Boolean(tooltip)"
                top
                transition="slide-y-transition"
                :activator="tooltip.activator"
                max-width="400"
            >
                <i18n :path="$typy(tooltip, 'iconTooltipTxt').safeString" tag="span">
                    <template v-if="tooltip.shortcut" v-slot:shortcut>
                        <b> {{ tooltip.shortcut }} </b>
                    </template>
                </i18n>
            </v-tooltip>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import PrefField from '@wsComps/PrefDlg/PrefField'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'

export default {
    name: 'pref-dlg',
    components: { PrefField },
    inheritAttrs: false,
    data() {
        return {
            preferences: {},
            tooltip: undefined,
            activePrefType: undefined,
        }
    },
    computed: {
        ...mapState({
            OS_KEY: state => state.mxsWorkspace.config.OS_KEY,
            PREF_TYPES: state => state.mxsWorkspace.config.PREF_TYPES,
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
            query_row_limit: state => state.prefAndStorage.query_row_limit,
            query_confirm_flag: state => state.prefAndStorage.query_confirm_flag,
            query_history_expired_time: state => state.prefAndStorage.query_history_expired_time,
            query_show_sys_schemas_flag: state => state.prefAndStorage.query_show_sys_schemas_flag,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
            max_statements: state => state.prefAndStorage.max_statements,
            identifier_auto_completion: state => state.prefAndStorage.identifier_auto_completion,
            def_conn_obj_type: state => state.prefAndStorage.def_conn_obj_type,
            interactive_timeout: state => state.prefAndStorage.interactive_timeout,
            wait_timeout: state => state.prefAndStorage.wait_timeout,
        }),
        isOpened: {
            get() {
                return this.$attrs.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        persistedPref() {
            return {
                query_row_limit: this.query_row_limit,
                /**
                 * Backward compatibility in older versions for query_confirm_flag and  query_show_sys_schemas_flag
                 * as the values are stored as either 0 or 1.
                 */
                query_confirm_flag: Boolean(this.query_confirm_flag),
                query_show_sys_schemas_flag: Boolean(this.query_show_sys_schemas_flag),
                // value is converted to number of days
                query_history_expired_time: this.$helpers.daysDiff(this.query_history_expired_time),
                tab_moves_focus: this.tab_moves_focus,
                max_statements: this.max_statements,
                identifier_auto_completion: this.identifier_auto_completion,
                def_conn_obj_type: this.def_conn_obj_type,
                interactive_timeout: this.interactive_timeout,
                wait_timeout: this.wait_timeout,
            }
        },
        objConnTypes() {
            const { LISTENERS, SERVERS, SERVICES } = this.MXS_OBJ_TYPES
            return [LISTENERS, SERVERS, SERVICES]
        },
        sysVariablesRefLink() {
            return 'https://mariadb.com/docs/server/ref/mdb/system-variables/'
        },
        prefFieldMap() {
            const { QUERY_EDITOR, CONN } = this.PREF_TYPES
            return {
                [QUERY_EDITOR]: {
                    positiveNumber: [
                        {
                            id: 'query_row_limit',
                            label: this.$mxs_t('rowLimit'),
                            icon: '$vuetify.icons.mxs_statusWarning',
                            iconColor: 'warning',
                            iconTooltipTxt: 'mxs.info.rowLimit',
                        },
                        {
                            id: 'max_statements',
                            label: this.$mxs_t('maxStatements'),
                            icon: '$vuetify.icons.mxs_statusWarning',
                            iconColor: 'warning',
                            iconTooltipTxt: 'mxs.info.maxStatements',
                        },
                        {
                            id: 'query_history_expired_time',
                            label: this.$mxs_t('queryHistoryRetentionPeriod'),
                            suffix: this.$mxs_t('days'),
                        },
                    ],
                    boolean: [
                        { id: 'query_confirm_flag', label: this.$mxs_t('showQueryConfirm') },
                        { id: 'query_show_sys_schemas_flag', label: this.$mxs_t('showSysSchemas') },
                        {
                            id: 'tab_moves_focus',
                            label: this.$mxs_t('tabMovesFocus'),
                            icon: 'mdi-information-outline',
                            iconColor: 'info',
                            iconTooltipTxt: this.preferences.tab_moves_focus
                                ? 'mxs.info.tabMovesFocus'
                                : 'mxs.info.tabInsetChar',
                            shortcut: `${this.OS_KEY} ${
                                this.$helpers.isMAC() ? '+ SHIFT' : ''
                            } + M`,
                        },
                        {
                            id: 'identifier_auto_completion',
                            label: this.$mxs_t('identifierAutoCompletion'),
                            icon: 'mdi-information-outline',
                            iconColor: 'info',
                            iconTooltipTxt: 'mxs.info.identifierAutoCompletion',
                        },
                    ],
                },
                [CONN]: {
                    enum: [
                        {
                            id: 'def_conn_obj_type',
                            label: this.$mxs_t('defConnObjType'),
                            enumValues: this.objConnTypes,
                        },
                    ],
                    positiveNumber: [
                        {
                            id: 'interactive_timeout',
                            label: 'interactive_timeout',
                            icon: '$vuetify.icons.mxs_questionCircle',
                            iconColor: 'info',
                            href: `${this.sysVariablesRefLink}/interactive_timeout/#DETAILS`,
                            isVariable: true,
                            suffix: 'seconds',
                        },
                        {
                            id: 'wait_timeout',
                            label: 'wait_timeout',
                            icon: '$vuetify.icons.mxs_questionCircle',
                            iconColor: 'info',
                            href: `${this.sysVariablesRefLink}/wait_timeout/#DETAILS`,
                            isVariable: true,
                            suffix: 'seconds',
                        },
                    ],
                },
            }
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.persistedPref, this.preferences)
        },
        activeQueryEditorConnId() {
            return this.$typy(QueryConn.getters('activeQueryEditorConn'), 'id').safeString
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) this.preferences = this.$helpers.lodash.cloneDeep(this.persistedPref)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'prefAndStorage/SET_QUERY_ROW_LIMIT',
            SET_QUERY_CONFIRM_FLAG: 'prefAndStorage/SET_QUERY_CONFIRM_FLAG',
            SET_QUERY_SHOW_SYS_SCHEMAS_FLAG: 'prefAndStorage/SET_QUERY_SHOW_SYS_SCHEMAS_FLAG',
            SET_QUERY_HISTORY_EXPIRED_TIME: 'prefAndStorage/SET_QUERY_HISTORY_EXPIRED_TIME',
            SET_TAB_MOVES_FOCUS: 'prefAndStorage/SET_TAB_MOVES_FOCUS',
            SET_MAX_STATEMENTS: 'prefAndStorage/SET_MAX_STATEMENTS',
            SET_IDENTIFIER_AUTO_COMPLETION: 'prefAndStorage/SET_IDENTIFIER_AUTO_COMPLETION',
            SET_DEF_CONN_OBJ_TYPE: 'prefAndStorage/SET_DEF_CONN_OBJ_TYPE',
            SET_INTERACTIVE_TIMEOUT: 'prefAndStorage/SET_INTERACTIVE_TIMEOUT',
            SET_WAIT_TIMEOUT: 'prefAndStorage/SET_WAIT_TIMEOUT',
        }),
        async onSave() {
            const diffs = this.$helpers.deepDiff(this.persistedPref, this.preferences)
            let systemVariables = []
            for (const diff of diffs) {
                const key = this.$typy(diff, 'path[0]').safeString
                let value = diff.rhs
                switch (key) {
                    case 'query_history_expired_time':
                        // Convert back to unix timestamp
                        value = this.$helpers.addDaysToNow(value)
                        break
                    case 'interactive_timeout':
                    case 'wait_timeout':
                        systemVariables.push(key)
                        break
                }
                this[`SET_${key.toUpperCase()}`](value)
            }
            if (this.activeQueryEditorConnId && systemVariables.length)
                await QueryConn.dispatch('setVariables', {
                    connId: this.activeQueryEditorConnId,
                    config: Worksheet.getters('activeRequestConfig'),
                    variables: systemVariables,
                })
        },
    },
}
</script>
<style lang="scss" scoped>
.pref-fields-ctr {
    min-height: 360px;
    max-height: 520px;
}
</style>

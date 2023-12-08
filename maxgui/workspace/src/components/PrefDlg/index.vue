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
                                        v-if="!$typy(preferences[field.name]).isNull"
                                        :key="field.id"
                                        v-model="preferences[field.name]"
                                        :type="type"
                                        :field="field"
                                        @tooltip="tooltip = $event"
                                    />
                                </template>
                            </template>
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
                <i18n :path="$typy(tooltip, 'i18nPath').safeString" tag="span">
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
                rowLimit: this.query_row_limit,
                showQueryConfirm: Boolean(this.query_confirm_flag),
                queryHistoryRetentionPeriod: this.$helpers.daysDiff(
                    this.query_history_expired_time
                ),
                showSysSchemas: Boolean(this.query_show_sys_schemas_flag),
                tabMovesFocus: this.tab_moves_focus,
                maxStatements: this.max_statements,
                identifierAutoCompletion: this.identifier_auto_completion,
                defConnObjType: this.def_conn_obj_type,
            }
        },
        objConnTypes() {
            const { LISTENERS, SERVERS, SERVICES } = this.MXS_OBJ_TYPES
            return [LISTENERS, SERVERS, SERVICES]
        },
        prefFieldMap() {
            const { QUERY_EDITOR, CONN } = this.PREF_TYPES
            return {
                [QUERY_EDITOR]: {
                    number: [
                        {
                            name: 'rowLimit',
                            icon: '$vuetify.icons.mxs_statusWarning',
                            iconColor: 'warning',
                            i18nPath: 'mxs.info.rowLimit',
                        },
                        {
                            name: 'maxStatements',
                            icon: '$vuetify.icons.mxs_statusWarning',
                            iconColor: 'warning',
                            i18nPath: 'mxs.info.maxStatements',
                        },
                        { name: 'queryHistoryRetentionPeriod', suffix: this.$mxs_t('days') },
                    ],
                    boolean: [
                        { name: 'showQueryConfirm' },
                        { name: 'showSysSchemas' },
                        {
                            name: 'tabMovesFocus',
                            icon: 'mdi-information-outline',
                            iconColor: 'info',
                            i18nPath: this.preferences.tabMovesFocus
                                ? 'mxs.info.tabMovesFocus'
                                : 'mxs.info.tabInsetChar',
                            shortcut: `${this.OS_KEY} ${
                                this.$helpers.isMAC() ? '+ SHIFT' : ''
                            } + M`,
                        },
                        {
                            name: 'identifierAutoCompletion',
                            icon: 'mdi-information-outline',
                            iconColor: 'info',
                            i18nPath: 'mxs.info.identifierAutoCompletion',
                        },
                    ],
                },
                //TODO: Add interactive_timeout,...
                [CONN]: {
                    enum: [{ name: 'defConnObjType', enumValues: this.objConnTypes }],
                },
            }
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.persistedPref, this.preferences)
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
        }),
        onSave() {
            this.SET_QUERY_ROW_LIMIT(this.preferences.rowLimit)
            this.SET_QUERY_CONFIRM_FLAG(Number(this.preferences.showQueryConfirm))
            this.SET_QUERY_HISTORY_EXPIRED_TIME(
                this.$helpers.addDaysToNow(this.preferences.queryHistoryRetentionPeriod)
            )
            this.SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(Number(this.preferences.showSysSchemas))
            this.SET_TAB_MOVES_FOCUS(this.preferences.tabMovesFocus)
            this.SET_MAX_STATEMENTS(this.preferences.maxStatements)
            this.SET_IDENTIFIER_AUTO_COMPLETION(this.preferences.identifierAutoCompletion)
            this.SET_DEF_CONN_OBJ_TYPE(this.preferences.defConnObjType)
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

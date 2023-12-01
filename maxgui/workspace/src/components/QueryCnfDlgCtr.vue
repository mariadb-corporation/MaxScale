<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_t('queryConfig')"
        :lazyValidation="false"
        minBodyWidth="512px"
        :hasChanged="hasChanged"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:form-body>
            <div v-for="field in numericFields" :key="field.name" class="px-1 py-2">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t(field.name) }}
                </label>
                <!-- Add key to trigger rerender when dialog is opened, otherwise the input will be empty -->
                <row-limit-ctr
                    v-if="field.name === 'rowLimit'"
                    :key="isOpened"
                    :height="36"
                    hide-details="auto"
                    class="vuetify-input--override error--text__bottom mb-2 rowLimit"
                    @change="config[field.name] = $event"
                />
                <template v-else>
                    <v-text-field
                        v-model.number="config[field.name]"
                        type="number"
                        :rules="[
                            v =>
                                validatePositiveNumber({
                                    v,
                                    inputName: $mxs_t(field.name),
                                }),
                        ]"
                        class="vuetify-input--override error--text__bottom mb-2"
                        :class="field.name"
                        dense
                        :height="36"
                        hide-details="auto"
                        outlined
                        required
                        :suffix="field.suffix"
                        @keypress="$helpers.preventNonNumericalVal($event)"
                    />
                </template>
                <template v-if="field.hasWarningInfo">
                    <v-icon size="16" color="warning" class="mr-2">
                        $vuetify.icons.mxs_alertWarning
                    </v-icon>
                    <small v-html="$mxs_t(`info.${field.name}`)" />
                </template>
            </div>
            <div v-for="field in boolFields" :key="field.name" class="pa-1">
                <v-checkbox
                    v-model="config[field.name]"
                    class="v-checkbox--mariadb pa-0 ma-0"
                    dense
                    :class="[field.name]"
                    color="primary"
                    hide-details="auto"
                >
                    <template v-slot:label>
                        <label class="v-label pointer">{{ $mxs_t(field.name) }}</label>
                        <v-tooltip
                            v-if="field.infoPath"
                            top
                            transition="slide-y-transition"
                            max-width="400"
                        >
                            <template v-slot:activator="{ on }">
                                <v-icon
                                    class="ml-1 material-icons-outlined pointer"
                                    size="16"
                                    color="info"
                                    v-on="on"
                                >
                                    mdi-information-outline
                                </v-icon>
                            </template>
                            <i18n :path="field.infoPath" tag="span">
                                <template v-if="field.shortcut" v-slot:shortcut>
                                    <b> {{ field.shortcut }} </b>
                                </template>
                            </i18n>
                        </v-tooltip>
                    </template>
                </v-checkbox>
            </div>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import RowLimitCtr from '@wkeComps/QueryEditor/RowLimitCtr.vue'
export default {
    name: 'query-cnf-dlg-ctr',
    components: { RowLimitCtr },
    inheritAttrs: false,
    data() {
        return {
            config: {},
        }
    },
    computed: {
        ...mapState({
            OS_KEY: state => state.mxsWorkspace.config.OS_KEY,
            query_row_limit: state => state.prefAndStorage.query_row_limit,
            query_confirm_flag: state => state.prefAndStorage.query_confirm_flag,
            query_history_expired_time: state => state.prefAndStorage.query_history_expired_time,
            query_show_sys_schemas_flag: state => state.prefAndStorage.query_show_sys_schemas_flag,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
            max_statements: state => state.prefAndStorage.max_statements,
            identifier_auto_completion: state => state.prefAndStorage.identifier_auto_completion,
        }),
        isOpened: {
            get() {
                return this.$attrs.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        persistedCnf() {
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
            }
        },
        numericFields() {
            return [
                { name: 'rowLimit', hasWarningInfo: true },
                { name: 'maxStatements', hasWarningInfo: true },
                { name: 'queryHistoryRetentionPeriod', suffix: this.$mxs_t('days') },
            ]
        },
        boolFields() {
            return [
                { name: 'showQueryConfirm' },
                { name: 'showSysSchemas' },
                {
                    name: 'tabMovesFocus',
                    infoPath: this.config.tabMovesFocus
                        ? 'mxs.info.tabMovesFocus'
                        : 'mxs.info.tabInsetChar',
                    shortcut: `${this.OS_KEY} ${this.$helpers.isMAC() ? '+ SHIFT' : ''} + M`,
                },
                {
                    name: 'identifierAutoCompletion',
                    infoPath: 'mxs.info.identifierAutoCompletion',
                },
            ]
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.persistedCnf, this.config)
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) this.config = this.$helpers.lodash.cloneDeep(this.persistedCnf)
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
        }),
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName })
            if (v > 0) return true
            return false
        },

        onSave() {
            this.SET_QUERY_ROW_LIMIT(this.config.rowLimit)
            this.SET_QUERY_CONFIRM_FLAG(Number(this.config.showQueryConfirm))
            this.SET_QUERY_HISTORY_EXPIRED_TIME(
                this.$helpers.addDaysToNow(this.config.queryHistoryRetentionPeriod)
            )
            this.SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(Number(this.config.showSysSchemas))
            this.SET_TAB_MOVES_FOCUS(this.config.tabMovesFocus)
            this.SET_MAX_STATEMENTS(this.config.maxStatements)
            this.SET_IDENTIFIER_AUTO_COMPLETION(this.config.identifierAutoCompletion)
        },
    },
}
</script>

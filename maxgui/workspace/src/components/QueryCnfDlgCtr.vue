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
            <div v-for="field in numericFields" :id="field.id" :key="field.name" class="px-1 py-2">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t(field.name) }}
                </label>
                <v-icon
                    v-if="field.icon"
                    size="14"
                    :color="field.iconColor"
                    class="ml-1 mb-1 pointer"
                    @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
                    @mouseleave="rmInfoTooltip"
                >
                    {{ field.icon }}
                </v-icon>
                <!-- Add key to trigger rerender when dialog is opened, otherwise the input will be empty -->
                <row-limit-ctr
                    v-if="field.name === 'rowLimit'"
                    :key="isOpened"
                    :height="36"
                    hide-details="auto"
                    class="vuetify-input--override error--text__bottom rowLimit"
                    @change="config[field.name] = $event"
                />
                <v-text-field
                    v-else
                    v-model.number="config[field.name]"
                    type="number"
                    :rules="[
                        v =>
                            validatePositiveNumber({
                                v,
                                inputName: $mxs_t(field.name),
                            }),
                    ]"
                    class="vuetify-input--override error--text__bottom"
                    :class="field.name"
                    dense
                    :height="36"
                    hide-details="auto"
                    outlined
                    required
                    :suffix="field.suffix"
                    @keypress="$helpers.preventNonNumericalVal($event)"
                />
            </div>
            <div v-for="field in boolFields" :id="field.id" :key="field.name" class="pa-1">
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
                        <v-icon
                            v-if="field.icon"
                            class="ml-1 material-icons-outlined pointer"
                            size="16"
                            :color="field.iconColor"
                            @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
                            @mouseleave="rmInfoTooltip"
                        >
                            {{ field.icon }}
                        </v-icon>
                    </template>
                </v-checkbox>
            </div>
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
            tooltip: undefined,
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
            ].map(field => ({ ...field, id: `activator_${this.$helpers.uuidv1()}` }))
        },
        boolFields() {
            return [
                { name: 'showQueryConfirm' },
                { name: 'showSysSchemas' },
                {
                    name: 'tabMovesFocus',
                    icon: 'mdi-information-outline',
                    iconColor: 'info',
                    i18nPath: this.config.tabMovesFocus
                        ? 'mxs.info.tabMovesFocus'
                        : 'mxs.info.tabInsetChar',
                    shortcut: `${this.OS_KEY} ${this.$helpers.isMAC() ? '+ SHIFT' : ''} + M`,
                },
                {
                    name: 'identifierAutoCompletion',
                    icon: 'mdi-information-outline',
                    iconColor: 'info',
                    i18nPath: 'mxs.info.identifierAutoCompletion',
                },
            ].map(field => ({ ...field, id: `activator_${this.$helpers.uuidv1()}` }))
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
        showInfoTooltip(data) {
            this.tooltip = data
        },
        rmInfoTooltip() {
            this.tooltip = undefined
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

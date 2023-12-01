<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_t('queryConfig')"
        :lazyValidation="false"
        minBodyWidth="512px"
        :hasChanged="hasChanged"
    >
        <template v-slot:form-body>
            <div class="px-1 py-2">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t('rowLimit') }}
                </label>
                <!-- Add key to trigger rerender when dialog is opened, otherwise the input will be empty -->
                <row-limit-ctr
                    :key="isOpened"
                    :height="36"
                    hide-details="auto"
                    class="vuetify-input--override error--text__bottom mb-2 rowLimit"
                    @change="config.rowLimit = $event"
                />
                <v-icon size="16" color="warning" class="mr-2">
                    $vuetify.icons.mxs_alertWarning
                </v-icon>
                <small v-html="$mxs_t('info.rowLimit')" />
            </div>
            <div class="px-1 py-2">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t('maxStatements') }}
                </label>
                <v-text-field
                    v-model.number="config.maxStatements"
                    type="number"
                    :rules="[
                        v =>
                            validatePositiveNumber({
                                v,
                                inputName: $mxs_t('maxStatements'),
                            }),
                    ]"
                    class="vuetify-input--override error--text__bottom mb-2 maxStatements"
                    dense
                    :height="36"
                    hide-details="auto"
                    outlined
                    required
                    @keypress="$helpers.preventNonNumericalVal($event)"
                />
                <v-icon size="16" color="warning" class="mr-2">
                    $vuetify.icons.mxs_alertWarning
                </v-icon>
                <small v-html="$mxs_t('info.maxStatements')" />
            </div>
            <div class="px-1 py-2">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t('queryHistoryRetentionPeriod') }} ({{ $mxs_t('inDays') }})
                </label>
                <v-text-field
                    v-model.number="config.queryHistoryRetentionPeriod"
                    type="number"
                    :rules="[
                        v =>
                            validatePositiveNumber({
                                v,
                                inputName: $mxs_t('queryHistoryRetentionPeriod'),
                            }),
                    ]"
                    class="vuetify-input--override error--text__bottom mb-2 queryHistoryRetentionPeriod"
                    dense
                    :height="36"
                    hide-details="auto"
                    outlined
                    required
                    @keypress="$helpers.preventNonNumericalVal($event)"
                />
            </div>
            <div
                v-for="(value, key) in $helpers.lodash.pick(config, [
                    'showQueryConfirm',
                    'showSysSchemas',
                    'tabMovesFocus',
                ])"
                :key="key"
                class="pa-1"
            >
                <v-checkbox
                    v-model="config[key]"
                    class="v-checkbox--mariadb pa-0 ma-0"
                    dense
                    :class="[key]"
                    color="primary"
                    hide-details="auto"
                >
                    <template v-slot:label>
                        <label class="v-label pointer">{{ $mxs_t(key) }}</label>
                        <v-tooltip
                            v-if="key === 'tabMovesFocus'"
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
                            <i18n
                                :path="
                                    config[key] ? 'mxs.info.tabMovesFocus' : 'mxs.info.tabInsetChar'
                                "
                                tag="span"
                            >
                                <template v-slot:shortcut>
                                    <b>
                                        {{ `${OS_KEY} ${$helpers.isMAC() ? '+ SHIFT' : ''}` }}
                                        + M
                                    </b>
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
/*
 * Emits
 * $emit('confirm-save', v:object): new cnf data
 */
import { mapState } from 'vuex'
import RowLimitCtr from '@wkeComps/QueryEditor/RowLimitCtr.vue'
export default {
    name: 'query-cnf-dlg',
    components: {
        RowLimitCtr,
    },
    props: {
        value: { type: Boolean, required: true },
        cnf: {
            type: Object,
            validator(obj) {
                return (
                    'query_row_limit' in obj &&
                    typeof obj.query_row_limit === 'number' &&
                    'query_confirm_flag' in obj &&
                    typeof obj.query_confirm_flag === 'number' &&
                    'query_history_expired_time' in obj &&
                    typeof obj.query_history_expired_time === 'number' &&
                    'query_show_sys_schemas_flag' in obj &&
                    typeof obj.query_show_sys_schemas_flag === 'number' &&
                    'tab_moves_focus' in obj &&
                    typeof obj.tab_moves_focus === 'boolean' &&
                    'max_statements' in obj &&
                    typeof obj.max_statements === 'number'
                )
            },
            required: true,
        },
    },
    data() {
        return {
            config: {},
        }
    },
    computed: {
        ...mapState({
            OS_KEY: state => state.mxsWorkspace.config.OS_KEY,
        }),
        isOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        defCnf() {
            return {
                rowLimit: this.cnf.query_row_limit,
                showQueryConfirm: Boolean(this.cnf.query_confirm_flag),
                queryHistoryRetentionPeriod: this.$helpers.daysDiff(
                    this.cnf.query_history_expired_time
                ),
                showSysSchemas: Boolean(this.cnf.query_show_sys_schemas_flag),
                tabMovesFocus: this.cnf.tab_moves_focus,
                maxStatements: this.cnf.max_statements,
            }
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.defCnf, this.config)
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) this.config = this.$helpers.lodash.cloneDeep(this.defCnf)
            },
        },
    },
    methods: {
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName })
            if (v > 0) return true
            return false
        },

        onSave() {
            this.$emit('confirm-save', {
                query_row_limit: this.config.rowLimit,
                query_confirm_flag: Number(this.config.showQueryConfirm),
                query_history_expired_time: this.$helpers.addDaysToNow(
                    this.config.queryHistoryRetentionPeriod
                ),
                query_show_sys_schemas_flag: Number(this.config.showSysSchemas),
                tab_moves_focus: this.config.tabMovesFocus,
                max_statements: this.config.maxStatements,
            })
        },
    },
}
</script>

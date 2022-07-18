<template>
    <base-dialog
        v-model="isOpened"
        :onSave="onSave"
        :title="$t('queryConfig')"
        :lazyValidation="false"
        minBodyWidth="512px"
        :hasChanged="hasChanged"
    >
        <template v-slot:form-body>
            <v-container class="pa-1">
                <v-row class="my-0 mx-n1">
                    <v-col cols="12" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('maxRows') }}
                        </label>
                        <!-- Add key to trigger rerender when dialog is opened, otherwise the input will be empty -->
                        <max-rows-input
                            :key="isOpened"
                            :height="36"
                            hide-details="auto"
                            :hasFieldsetBorder="false"
                            class="std error--text__bottom mb-2 maxRows"
                            @change="config.maxRows = $event"
                        />
                        <v-icon size="16" color="warning" class="mr-2">
                            $vuetify.icons.alertWarning
                        </v-icon>
                        <small v-html="$t('info.maxRows')" />
                    </v-col>
                    <v-col cols="12" class="pa-1 mb-3">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('queryHistoryRetentionPeriod') }} ({{ $t('inDays') }})
                        </label>
                        <v-text-field
                            v-model.number="config.queryHistoryRetentionPeriod"
                            type="number"
                            :rules="rules.queryHistoryRetentionPeriod"
                            class="std error--text__bottom mb-2 queryHistoryRetentionPeriod"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                            @keypress="$help.preventNonNumericalVal($event)"
                        />
                    </v-col>
                    <v-col
                        v-for="(value, key) in $help.lodash.pick(config, [
                            'showQueryConfirm',
                            'showSysSchemas',
                        ])"
                        :key="key"
                        cols="12"
                        class="pa-1"
                    >
                        <v-checkbox
                            v-model="config[key]"
                            class="config-checkbox pa-0 ma-0"
                            :class="[key]"
                            :label="$t(key)"
                            color="primary"
                            hide-details="auto"
                        />
                    </v-col>
                </v-row>
            </v-container>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Emits
 * $emit('confirm-save', v:object): new cnf data
 */
import MaxRowsInput from './MaxRowsInput.vue'
export default {
    name: 'query-cnf-dlg',
    components: {
        'max-rows-input': MaxRowsInput,
    },
    props: {
        value: { type: Boolean, required: true },
        cnf: {
            type: Object,
            validator(obj) {
                return (
                    'query_max_rows' in obj &&
                    typeof obj.query_max_rows === 'number' &&
                    'query_confirm_flag' in obj &&
                    typeof obj.query_confirm_flag === 'number' &&
                    'query_history_expired_time' in obj &&
                    typeof obj.query_history_expired_time === 'number' &&
                    'query_show_sys_schemas_flag' in obj &&
                    typeof obj.query_show_sys_schemas_flag === 'number'
                )
            },
            required: true,
        },
    },
    data() {
        return {
            rules: {
                queryHistoryRetentionPeriod: [
                    v =>
                        this.validatePositiveNumber({
                            v,
                            inputName: this.$t('queryHistoryRetentionPeriod'),
                        }),
                ],
            },
            config: {},
        }
    },
    computed: {
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
                maxRows: this.cnf.query_max_rows,
                showQueryConfirm: Boolean(this.cnf.query_confirm_flag),
                queryHistoryRetentionPeriod: this.$help.daysDiff(
                    this.cnf.query_history_expired_time
                ),
                showSysSchemas: Boolean(this.cnf.query_show_sys_schemas_flag),
            }
        },
        hasChanged() {
            return !this.$help.lodash.isEqual(this.defCnf, this.config)
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) this.config = this.$help.lodash.cloneDeep(this.defCnf)
            },
        },
    },
    methods: {
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString) return this.$t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$t('errors.largerThanZero', { inputName })
            if (v > 0) return true
            return false
        },

        onSave() {
            this.$emit('confirm-save', {
                query_max_rows: this.config.maxRows,
                query_confirm_flag: Number(this.config.showQueryConfirm),
                query_history_expired_time: this.$help.addDaysToNow(
                    this.config.queryHistoryRetentionPeriod
                ),
                query_show_sys_schemas_flag: Number(this.config.showSysSchemas),
            })
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .config-checkbox {
    label {
        font-size: $label-control-size;
        color: $small-text;
    }
}
</style>

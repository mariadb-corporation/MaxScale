<template>
    <base-dialog
        ref="connDialog"
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
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import MaxRowsInput from './MaxRowsInput.vue'
export default {
    name: 'query-config-dialog',
    components: {
        'max-rows-input': MaxRowsInput,
    },
    props: {
        value: { type: Boolean, required: true },
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
            curCnf: {},
            config: {
                maxRows: 10000,
                showQueryConfirm: true,
                queryHistoryRetentionPeriod: 0,
                showSysSchemas: true,
            },
        }
    },
    computed: {
        ...mapState({
            query_max_rows: state => state.persisted.query_max_rows,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            query_history_expired_time: state => state.persisted.query_history_expired_time,
            query_show_sys_schemas_flag: state => state.persisted.query_show_sys_schemas_flag,
        }),
        isOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        hasChanged() {
            return !this.$help.lodash.isEqual(this.curCnf, this.config)
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) {
                    this.setCurCnf()
                    this.config = this.$help.lodash.cloneDeep(this.curCnf)
                }
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_QUERY_MAX_ROW: 'persisted/SET_QUERY_MAX_ROW',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_QUERY_HISTORY_EXPIRED_TIME: 'persisted/SET_QUERY_HISTORY_EXPIRED_TIME',
            SET_QUERY_SHOW_SYS_SCHEMAS_FLAG: 'persisted/SET_QUERY_SHOW_SYS_SCHEMAS_FLAG',
        }),
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString) return this.$t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$t('errors.largerThanZero', { inputName })
            if (v > 0) return true
        },
        setCurCnf() {
            this.curCnf = {
                maxRows: this.query_max_rows,
                showQueryConfirm: Boolean(this.query_confirm_flag),
                queryHistoryRetentionPeriod: this.$help.daysDiff(this.query_history_expired_time),
                showSysSchemas: Boolean(this.query_show_sys_schemas_flag),
            }
        },
        onSave() {
            this.SET_QUERY_MAX_ROW(this.config.maxRows)
            this.SET_QUERY_CONFIRM_FLAG(Number(this.config.showQueryConfirm))
            this.SET_QUERY_HISTORY_EXPIRED_TIME(
                this.$help.addDaysToNow(this.config.queryHistoryRetentionPeriod)
            )
            this.SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(Number(this.config.showSysSchemas))
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

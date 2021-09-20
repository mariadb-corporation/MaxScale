<template>
    <base-dialog
        ref="connDialog"
        v-model="isOpened"
        :onSave="onSave"
        :title="`Query configuration`"
        :lazyValidation="false"
        minBodyWidth="512px"
        :hasChanged="hasChanged"
    >
        <template v-slot:form-body>
            <v-container class="pa-1">
                <v-row class="mx-n1">
                    <v-col cols="12" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('maxRows') }}
                        </label>
                        <v-text-field
                            v-model.number="config.maxRows"
                            type="number"
                            :rules="rules.maxRows"
                            class="std error--text__bottom mb-2"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        />
                        <v-icon size="16" color="warning" class="mr-2">
                            $vuetify.icons.alertWarning
                        </v-icon>
                        <small v-html="$t('info.maxRows')" />
                    </v-col>

                    <v-col cols="12" class="pa-1 mt-3">
                        <v-switch
                            v-model="config.showQueryConfirm"
                            :label="
                                $t('info.queryShowConfirm', {
                                    action: config.showQueryConfirm ? $t('show') : $t('hide'),
                                })
                            "
                            hide-details="auto"
                            class="show-confirm-switch mt-0 pa-0"
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
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'query-config-dialog',
    props: {
        value: { type: Boolean, required: true },
    },
    data() {
        return {
            rules: {
                maxRows: [
                    val => {
                        if (this.$typy(val).isString || val < 0)
                            return this.$t('errors.negativeNum')
                        if (val === 0)
                            return this.$t('errors.largerThanZero', { inputName: 'Max rows' })
                        if (val > 0) return true
                        return this.$t('errors.requiredInput', { inputName: 'Max rows' })
                    },
                ],
            },
            defConfig: {},
            config: {
                maxRows: 10000,
                showQueryConfirm: true,
            },
        }
    },
    computed: {
        ...mapState({
            query_max_rows: state => state.query.query_max_rows,
            query_confirm_flag: state => state.query.query_confirm_flag,
        }),
        isOpened: {
            get() {
                if (this.value) this.$emit('on-open')
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        hasChanged() {
            if (!this.isOpened) return false
            return !this.$help.lodash.isEqual(this.defConfig, this.config)
        },
    },
    watch: {
        isOpened(v) {
            if (v) {
                this.handleSetDefConfig()
                this.config = this.$help.lodash.cloneDeep(this.defConfig)
            }
        },
    },
    methods: {
        ...mapMutations({
            SET_QUERY_MAX_ROW: 'query/SET_QUERY_MAX_ROW',
            SET_QUERY_CONFIRM_FLAG: 'query/SET_QUERY_CONFIRM_FLAG',
        }),
        handleSetDefConfig() {
            if (isNaN(this.query_max_rows)) this.SET_QUERY_MAX_ROW(10000)
            if (isNaN(this.query_confirm_flag)) this.SET_QUERY_CONFIRM_FLAG(1)
            this.defConfig.maxRows = this.query_max_rows
            this.defConfig.showQueryConfirm = Boolean(this.query_confirm_flag)
        },
        onSave() {
            this.SET_QUERY_MAX_ROW(this.config.maxRows)
            this.SET_QUERY_CONFIRM_FLAG(Number(this.config.showQueryConfirm))
        },
    },
}
</script>

<style lang="scss" scoped>
$label-size: 0.75rem;
.field__label {
    font-size: $label-size;
}
::v-deep .show-confirm-switch {
    label {
        font-size: $label-size;
        color: $small-text;
    }
}
</style>

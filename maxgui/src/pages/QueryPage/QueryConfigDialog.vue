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
                            v-model.number="maxRows"
                            type="number"
                            :rules="rules.maxRows"
                            class="std error--text__bottom mb-2"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        />
                        <small v-html="$t('info.maxRows')" />
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
 * Change Date: 2025-03-24
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
            maxRows: 0,
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
        }
    },
    computed: {
        ...mapState({ query_max_rows: state => state.query.query_max_rows }),
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
            return this.query_max_rows !== this.maxRows
        },
    },
    watch: {
        isOpened(v) {
            if (v) this.maxRows = this.query_max_rows
            // reset to initial state and bind this context
            else Object.assign(this.$data, this.$options.data.apply(this))
        },
    },
    methods: {
        ...mapMutations({ SET_QUERY_MAX_ROW: 'query/SET_QUERY_MAX_ROW' }),
        onSave() {
            this.SET_QUERY_MAX_ROW(this.maxRows)
        },
    },
}
</script>

<style lang="scss" scoped>
$label-size: 0.75rem;
.field__label {
    font-size: $label-size;
}
</style>

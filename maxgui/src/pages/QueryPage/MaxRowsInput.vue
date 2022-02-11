<template>
    <!-- max-rows-dropdown-->
    <!-- Use input.native to update value as a workaround
         for https://github.com/vuetifyjs/vuetify/issues/4679
    -->
    <v-combobox
        v-model="maxRows"
        :items="SQL_DEF_MAX_ROWS_OPTS"
        outlined
        dense
        class="std mariadb-select-input max-rows-dropdown"
        :class="{ 'max-rows-dropdown--fieldset-border': hasFieldsetBorder }"
        :menu-props="{
            contentClass: 'mariadb-select-v-menu',
            bottom: true,
            offsetY: true,
        }"
        :height="height"
        :hide-details="hideDetails"
        return-object
        :rules="rules.maxRows"
        @input.native="onInput"
        @keypress="$help.preventNonNumericalVal($event)"
    >
        <template v-slot:prepend-inner>
            <slot name="prepend-inner" />
        </template>
    </v-combobox>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapState } from 'vuex'
export default {
    name: 'max-rows-input',
    props: {
        height: { type: Number, default: 36 },
        hideDetails: { type: [Boolean, String], default: true },
        hasFieldsetBorder: { type: Boolean, default: true },
    },
    data() {
        return {
            rules: {
                maxRows: [obj => this.handleValidateMaxRows(obj)],
            },
            maxRows: {},
        }
    },
    computed: {
        ...mapState({
            SQL_DEF_MAX_ROWS_OPTS: state => state.app_config.SQL_DEF_MAX_ROWS_OPTS,
            query_max_rows: state => state.persisted.query_max_rows,
        }),
    },
    watch: {
        maxRows: {
            deep: true,
            handler(obj) {
                if (this.$typy(obj, 'value').isNumber) this.$emit('change', obj.value)
            },
        },
        // When query_max_rows value is changed elsewhere, it should be updated
        query_max_rows(v) {
            if (this.maxRows && this.maxRows.value !== v) this.updateMaxRows()
        },
    },
    created() {
        this.updateMaxRows()
    },
    methods: {
        updateMaxRows() {
            this.maxRows = this.genDropDownItem(this.query_max_rows)
        },
        handleValidateMaxRows(v) {
            if (this.$typy(v, 'value').isEmptyString)
                return this.$t('errors.requiredInput', { inputName: this.$t('maxRows') })
            else if (!this.$typy(v, 'value').isNumber) return this.$t('errors.nonInteger')
            else if (v.value < 0)
                return this.$t('errors.largerThanZero', { inputName: this.$t('maxRows') })
            return true
        },
        genDropDownItem(v) {
            let num = parseInt(v)
            if (!this.$typy(num).isNumber) return { text: '', value: '' }
            return { text: num === 0 ? `Don't Limit` : num, value: num }
        },
        onInput(evt) {
            this.maxRows = this.genDropDownItem(evt.srcElement.value)
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.max-rows-dropdown {
    .v-input__control {
        .v-input__prepend-inner {
            display: flex;
            height: 100%;
            align-items: center;
            margin-top: 0px !important;
            width: 68px;
        }
    }
}
::v-deep.max-rows-dropdown--fieldset-border {
    .v-input__control {
        fieldset {
            border: thin solid $accent-dark;
        }
    }
}
</style>

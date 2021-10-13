<template>
    <v-combobox
        v-if="input.type === 'column_type'"
        v-model="input.value"
        class="std mariadb-select-input error--text__bottom error--text__bottom--no-margin"
        :class="input.type"
        :menu-props="{
            contentClass: 'mariadb-select-v-menu',
            bottom: true,
            offsetY: true,
            openOnClick: false,
        }"
        :items="input.enum_values"
        item-text="value"
        item-value="value"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :return-object="false"
        @change="handleChange"
    />
    <v-checkbox
        v-else-if="input.type === 'bool'"
        v-model="input.value"
        dense
        class="checkbox--scale-reduce ma-0 pa-0"
        primary
        hide-details
        :disabled="isDisabled"
        @change="handleChange"
    />
    <charset-input
        v-else-if="input.type === 'charset'"
        v-model="input.value"
        :defCharset="defTblCharset"
        :height="height"
        :disabled="isDisabled"
        @on-change="handleChange"
    />
    <collation-input
        v-else-if="input.type === 'collation'"
        v-model="input.value"
        :height="height"
        :defCollation="defTblCollation"
        :charset="columnCharset"
        :disabled="isDisabled"
        @on-change="handleChange"
    />
    <v-text-field
        v-else
        v-model="input.value"
        class="std error--text__bottom error--text__bottom--no-margin"
        :class="`${input.type}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
        @change="handleChange"
    />
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
/*
 *
 * data: {
 *  field?: string, header name
 *  value?: string, cell value
 *  rowObj?: object, entire row data
 * }
 * Events
 * Below events are used to handle "coupled case",
 * e.g. When column_type changes its value to a data type
 * that supports charset/collation, `on-change-column_type`
 * will be used to update charset/collation input to fill
 * data with default table charset/collation.
 * on-change-column_type: (cell)
 * on-change-charset: (cell)
 * on-change-AI: (cell)
 * Event for normal cell
 * on-change: (cell)
 */

import CharsetInput from './CharsetInput.vue'
import CollationInput from './CollationInput.vue'
import { check_charset_support, check_UN_ZF_support, check_AI_support } from './colOptHelpers'
export default {
    name: 'column-input',
    components: {
        'charset-input': CharsetInput,
        'collation-input': CollationInput,
    },
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        // for data type supports charset/collation
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
        dataTypes: { type: Array, default: () => [] },
    },
    data() {
        return {
            input: {},
        }
    },
    computed: {
        hasChanged() {
            return !this.$help.lodash.isEqual(this.input, this.data)
        },
        columnCharset() {
            return this.$typy(this.input, 'rowObj.charset').safeString
        },
        columnType() {
            return this.$typy(this.input, 'rowObj.column_type').safeString
        },
        isDisabled() {
            switch (this.$typy(this.input, 'field').safeString) {
                case 'charset':
                case 'collation':
                    if (this.columnCharset === 'utf8') return true
                    return !check_charset_support(this.columnType)
                case 'UN':
                case 'ZF':
                    return !check_UN_ZF_support(this.columnType)
                case 'AI':
                    return !check_AI_support(this.columnType)
                default:
                    return false
            }
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v, oV) {
                if (!this.$help.lodash.isEqual(v, oV)) this.initInputType(v)
            },
        },
    },
    created() {
        this.initInputType(this.data)
    },
    methods: {
        initInputType(data) {
            this.input = this.handleAddType(data)
        },
        /**
         * This function handles adding type and necessary attribute to input
         * base on field name
         * @param {Object} data - initial input data
         * @returns {Object} - returns copied of data with necessary properties to render
         * appropriate input type
         */
        handleAddType(data) {
            const input = this.$help.lodash.cloneDeep(data)
            switch (input.field) {
                case 'column_name':
                    input.type = 'string'
                    break
                case 'column_type':
                    input.type = 'column_type'
                    input.enum_values = this.dataTypes
                    break
                case 'PK':
                case 'NN':
                case 'UN':
                case 'ZF':
                case 'AI':
                    input.type = 'bool'
                    input.value = input.value === 'YES'
                    break
                case 'charset':
                case 'collation':
                    input.type = input.field
                    break
            }
            return input
        },
        /**
         * This function basically undo what handleAddType did
         * @returns {Object} - returns input object with same properties as data props
         */
        handleRemoveType() {
            const newInput = this.$help.lodash.cloneDeep(this.input)
            delete newInput.type
            delete newInput.enum_values
            return newInput
        },
        handleChange() {
            if (this.hasChanged) {
                let newInput = this.handleRemoveType()
                switch (this.input.type) {
                    case 'column_type':
                        this.$emit('on-change-column_type', newInput)
                        break
                    case 'charset':
                        this.$emit('on-change-charset', newInput)
                        break
                    case 'bool': {
                        const field = newInput.field
                        switch (field) {
                            case 'PK':
                            case 'NN':
                            case 'UN':
                            case 'ZF':
                            case 'AI':
                                newInput.value = newInput.value ? 'YES' : 'NO'
                                break
                        }
                        if (field === 'AI') this.$emit('on-change-AI', newInput)
                        else this.$emit('on-change', newInput)
                        break
                    }
                    default:
                        this.$emit('on-change', newInput)
                        break
                }
            }
        },
    },
}
</script>

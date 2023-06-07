<template>
    <v-combobox
        v-if="input.type === COL_ATTRS.TYPE"
        v-model="input.value"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="input.type"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="input.enum_values"
        item-text="value"
        item-value="value"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :return-object="false"
        @input="onInput"
    />
    <v-select
        v-else-if="input.type === 'enum'"
        v-model="input.value"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="input.type"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="input.enum_values"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :disabled="isDisabled"
        @input="onInput"
    />
    <v-checkbox
        v-else-if="input.type === 'bool'"
        v-model="input.value"
        dense
        class="v-checkbox--mariadb-xs ma-0 pa-0"
        primary
        hide-details
        :disabled="isDisabled"
        @change="onInput"
    />
    <charset-collate-select
        v-else-if="input.type === COL_ATTRS.CHARSET || input.type === COL_ATTRS.COLLATE"
        v-model="input.value"
        :items="
            input.type === COL_ATTRS.CHARSET
                ? Object.keys(charsetCollationMap)
                : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
        "
        :defItem="input.type === COL_ATTRS.CHARSET ? defTblCharset : defTblCollation"
        :disabled="isDisabled"
        :height="height"
        @input="onInput"
    />
    <v-text-field
        v-else
        v-model="input.value"
        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
        :class="`${input.type}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
        @input="onInput"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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
 *  alterColIdx?: number, index of the column being altered
 *  colOptIdx?: number, index of the column option. e.g. index of PK, NN, UN ,...
 * }
 * Events
 * Below events are used to handle "coupled case",
 * e.g. When type changes its value to a data type
 * that supports charset/collation, `on-input-type`
 * will be used to update charset/collation input to fill
 * data with default table charset/collation.
 * on-input-type: (cell)
 * on-input-charset: (cell)
 * on-input-AI: (cell)
 * on-input-PK: (cell)
 * on-input-NN: (cell)
 * on-input-generated: (cell)
 * Event for normal cell
 * on-input: (cell)
 */
import { mapState } from 'vuex'
import CharsetCollateSelect from '@wsSrc/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
import {
    check_charset_support,
    check_UN_ZF_support,
    check_AI_support,
} from '@wsSrc/components/common/MxsDdlEditor/utils'
export default {
    name: 'col-opt-input',
    components: {
        CharsetCollateSelect,
    },
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        charsetCollationMap: { type: Object, required: true },
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
        dataTypes: { type: Array, default: () => [] },
        initialColOptsData: { type: Array, required: true },
    },
    data() {
        return {
            input: {},
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
        }),
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.input, this.data)
        },
        columnCharset() {
            return this.$typy(this.input, `rowObj.${this.COL_ATTRS.CHARSET}`).safeString
        },
        columnType() {
            return this.$typy(this.input, `rowObj.${this.COL_ATTRS.TYPE}`).safeString
        },
        isPK() {
            return this.$typy(this.input, `rowObj.${this.COL_ATTRS.PK}`).safeString === 'YES'
        },
        isGenerated() {
            return (
                this.$typy(this.input, `rowObj.${this.COL_ATTRS.GENERATED}`).safeString !== '(none)'
            )
        },
        isAI() {
            return (
                this.$typy(this.input, `rowObj.${this.COL_ATTRS.AI}`).safeString ===
                'AUTO_INCREMENT'
            )
        },
        uniqueIdxName() {
            // If there's name already, use it otherwise generate one with this pattern `columnName_UNIQUE`
            const uqIdxName = this.$typy(this.initialColOptsData, `['${this.data.colOptIdx}']`)
                .safeString
            if (uqIdxName) return uqIdxName
            return `${this.$typy(this.data, `rowObj.${this.COL_ATTRS.NAME}`).safeString}_UNIQUE`
        },
        isDisabled() {
            const { PK, NN, UN, UQ, ZF, AI, GENERATED, CHARSET, COLLATE } = this.COL_ATTRS
            switch (this.$typy(this.input, 'field').safeString) {
                case CHARSET:
                case COLLATE:
                    if (this.columnCharset === 'utf8') return true
                    return !check_charset_support(this.columnType)
                case PK:
                    //disable if column is generated
                    return this.isGenerated
                case UN:
                case ZF:
                    return !check_UN_ZF_support(this.columnType)
                case AI:
                    return !check_AI_support(this.columnType)
                case NN:
                    //isAI or isPK implies NOT NULL so must be disabled
                    // when column is generated, NN or NULL can not be defined
                    return this.isAI || this.isPK || this.isGenerated
                case UQ:
                    return this.isPK // implies UNIQUE already so UQ must be disabled
                case GENERATED:
                    //disable if column is PK
                    return this.isPK //https://mariadb.com/kb/en/generated-columns/#index-support
                default:
                    return false
            }
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.initInputType(v)
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
            const input = this.$helpers.lodash.cloneDeep(data)
            const {
                NAME,
                TYPE,
                PK,
                NN,
                UN,
                UQ,
                ZF,
                AI,
                GENERATED,
                CHARSET,
                COLLATE,
            } = this.COL_ATTRS
            const { nn, un, zf, ai } = this.CREATE_TBL_TOKENS
            switch (input.field) {
                case NAME:
                    input.type = 'string'
                    break
                case TYPE:
                    input.type = 'type'
                    input.enum_values = this.dataTypes
                    break
                case NN:
                    input.type = 'bool'
                    input.value = input.value === nn
                    break
                case UN:
                    input.type = 'bool'
                    input.value = input.value === un
                    break
                case ZF:
                    input.type = 'bool'
                    input.value = input.value === zf
                    break
                case AI:
                    input.type = 'bool'
                    input.value = input.value === ai
                    break
                case GENERATED:
                    input.type = 'enum'
                    input.enum_values = ['(none)', 'VIRTUAL', 'STORED']
                    break
                case PK:
                    input.type = 'bool'
                    input.value = input.value === 'YES'
                    break
                case UQ:
                    input.type = 'bool'
                    input.value = Boolean(input.value)
                    break
                case CHARSET:
                case COLLATE:
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
            const newInput = this.$helpers.lodash.cloneDeep(this.input)
            delete newInput.type
            delete newInput.enum_values
            return newInput
        },
        onInput() {
            if (this.hasChanged) {
                let newInput = this.handleRemoveType()
                const { TYPE, PK, NN, UN, UQ, ZF, AI, GENERATED, CHARSET } = this.COL_ATTRS
                const { nn, un, zf, ai } = this.CREATE_TBL_TOKENS
                switch (this.input.type) {
                    case TYPE:
                        this.$emit('on-input-type', newInput)
                        break
                    case CHARSET:
                        this.$emit('on-input-charset', newInput)
                        break
                    case 'enum':
                        if (newInput.field === GENERATED) this.$emit('on-input-generated', newInput)
                        else this.$emit('on-input', newInput)
                        break
                    case 'bool': {
                        const field = newInput.field
                        switch (field) {
                            case NN:
                                newInput.value = newInput.value ? nn : this.CREATE_TBL_TOKENS.null
                                break
                            case UN:
                                newInput.value = newInput.value ? un : ''
                                break
                            case ZF:
                                newInput.value = newInput.value ? zf : ''
                                break
                            case AI:
                                newInput.value = newInput.value ? ai : ''
                                break
                            case PK:
                                newInput.value = newInput.value ? 'YES' : 'NO'
                                break
                            case UQ:
                                newInput.value = newInput.value ? this.uniqueIdxName : ''
                        }
                        if (field === AI) this.$emit('on-input-AI', newInput)
                        else if (field === PK) this.$emit('on-input-PK', newInput)
                        else if (field === NN) this.$emit('on-input-NN', newInput)
                        else this.$emit('on-input', newInput)
                        break
                    }
                    default:
                        this.$emit('on-input', newInput)
                        break
                }
            }
        },
    },
}
</script>

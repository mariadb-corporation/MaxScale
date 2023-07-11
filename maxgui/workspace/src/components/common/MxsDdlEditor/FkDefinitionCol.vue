<template>
    <v-text-field
        v-if="data.field === FK_EDITOR_ATTRS.NAME"
        v-model="inputValue"
        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
        :class="`${data.field}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
    />
    <v-select
        v-else
        v-model="inputValue"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="data.field"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="enumValues"
        item-text="text"
        item-value="id"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :multiple="
            data.field === FK_EDITOR_ATTRS.REFERENCING_COL ||
                data.field === FK_EDITOR_ATTRS.REFERENCED_COL
        "
    />
    <!-- TODO: Add an option for REFERENCED_TARGET input to manually type in new target -->
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
 *  value?:  (string | string[]), cell value
 *  rowIdx?: number, row index of the key being altered
 * }
 */
import { mapState } from 'vuex'
export default {
    name: 'fk-definition-col',
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        referencingColOptions: { type: Array, required: true },
        referencedTargets: { type: Array, required: true },
        referencedColOptions: { type: Array, required: true },
    },
    computed: {
        ...mapState({
            FK_EDITOR_ATTRS: state => state.mxsWorkspace.config.FK_EDITOR_ATTRS,
            REFERENCE_OPTIONS: state => state.mxsWorkspace.config.REFERENCE_OPTIONS,
        }),
        inputValue: {
            get() {
                return this.data.value
            },
            set(v) {
                this.$emit('on-input', { ...this.data, value: v })
            },
        },
        enumValues() {
            const {
                REFERENCING_COL,
                REFERENCED_TARGET,
                REFERENCED_COL,
                ON_UPDATE,
                ON_DELETE,
            } = this.FK_EDITOR_ATTRS
            switch (this.data.field) {
                case REFERENCING_COL:
                    return this.referencingColOptions
                case REFERENCED_TARGET:
                    return this.referencedTargets
                case REFERENCED_COL:
                    return this.referencedColOptions
                case ON_UPDATE:
                case ON_DELETE:
                    return Object.values(this.REFERENCE_OPTIONS)
                default:
                    return []
            }
        },
    },
}
</script>

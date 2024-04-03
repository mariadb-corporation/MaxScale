<template>
    <v-container
        fluid
        class="annotation-cnf mxs-color-helper all-border-table-border rounded pa-0 relative"
    >
        <v-row class="mt-3 mr-4 mb-0 ml-2">
            <mxs-tooltip-btn
                small
                icon
                depressed
                color="error"
                btnClass="delete-btn absolute"
                @click="$emit('on-delete')"
            >
                <template v-slot:btn-content>
                    <v-icon size="10"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('delete') }}
            </mxs-tooltip-btn>
            <v-col v-for="field in annotationFields" :key="field.id" cols="4" class="pa-0 pl-2">
                <mxs-cnf-field
                    v-if="!$typy(getFieldValue(field)).isNull"
                    :value="getFieldValue(field)"
                    :type="field.type"
                    :field="field"
                    :height="32"
                    @input="
                        field.type === 'color'
                            ? onUpdateColor({ field, value: $event })
                            : setFieldValue({ field, value: $event })
                    "
                    @click.native="field.type === 'color' ? onClickColorInput(field) : null"
                />
            </v-col>
            <v-menu
                v-if="activeColorField"
                :key="`#${activeColorField.id}`"
                :value="Boolean(activeColorField)"
                :activator="`#${activeColorField.id}`"
                :close-on-content-click="false"
                offset-y
                bottom
                nudge-top="12"
            >
                <v-color-picker
                    hide-mode-switch
                    mode="hexa"
                    :value="getFieldValue(activeColorField)"
                    @update:color="onUpdateColor({ field: activeColorField, value: $event.hex })"
                />
            </v-menu>
        </v-row>
    </v-container>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'annotation-cnf',
    props: {
        value: { type: Object, required: true },
    },
    data() {
        return {
            activeGraphName: '',
            graphsCnf: {},
            activeColorField: null,
        }
    },
    computed: {
        data: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        annotationFields() {
            return [
                { dataId: 'content', label: this.$mxs_t('label'), type: 'string', isLabel: true },
                { dataId: 'yMax', label: this.$mxs_t('maxValue'), type: 'nonNegativeNumber' },
                { dataId: 'yMin', label: this.$mxs_t('minValue'), type: 'nonNegativeNumber' },
                {
                    dataId: 'backgroundColor',
                    label: this.$mxs_t('labelBgColor'),
                    type: 'color',
                    isLabel: true,
                },
                {
                    dataId: 'color',
                    label: this.$mxs_t('labelTxtColor'),
                    type: 'color',
                    isLabel: true,
                },
                { dataId: 'borderColor', label: this.$mxs_t('lineColor'), type: 'color' },
            ].map(field => ({ ...field, id: `field_${this.$helpers.uuidv1()}` }))
        },
    },
    created() {
        this.setColorFieldValue = this.$helpers.lodash.debounce(({ field, value }) => {
            if (value) {
                const isValidColor = this.$helpers.validateHexColor(value)
                if (isValidColor) this.setFieldValue({ field, value })
            }
        }, 300)
    },
    methods: {
        getFieldValue(field) {
            return field.isLabel ? this.data.label[field.dataId] : this.data[field.dataId]
        },
        setFieldValue({ field, value }) {
            if (field.isLabel) this.data.label[field.dataId] = value
            else this.data[field.dataId] = value
        },
        onClickColorInput(v) {
            this.activeColorField = v
        },
        onUpdateColor({ field, value }) {
            this.setColorFieldValue({ field, value })
        },
    },
}
</script>

<style lang="scss">
.annotation-cnf {
    .delete-btn {
        top: 4px;
        right: 4px;
    }
}
</style>

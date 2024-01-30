<template>
    <div class="annotations-cnf-ctr">
        <div class="d-flex align-center mb-4 justify-space-between">
            <p
                data-test="headline"
                class="mb-0 text-body-2 font-weight-bold mxs-color-helper text-navigation text-uppercase"
            >
                {{ cnfType }}
            </p>
            <v-btn
                v-if="annotationsLength > 0"
                color="primary"
                text
                x-small
                data-test="add-btn"
                class="add-btn text-capitalize"
                @click="onAdd"
            >
                + {{ $mxs_t('add') }}
            </v-btn>
        </div>
        <template v-for="(data, key) in annotations">
            <annotation-cnf
                v-if="!$typy(annotations, key).isEmptyObject"
                :key="key"
                v-model="annotations[key]"
                class="mb-5"
                @on-delete="onDelete(key)"
            />
        </template>
        <div
            v-if="annotationsLength === 0"
            v-ripple
            data-test="add-btn-block"
            class="add-btn-block d-flex align-center justify-center mxs-color-helper all-border-table-border rounded text-primary pointer"
            @click="onAdd"
        >
            + {{ $mxs_t('add') }}
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import AnnotationCnf from '@src/pages/Dashboard/AnnotationCnf'

export default {
    name: 'annotations-cnf-ctr',
    components: { AnnotationCnf },
    props: {
        value: { type: Object, required: true },
        cnfType: { type: String, required: true },
    },
    computed: {
        annotations: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        annotationsLength() {
            return Object.keys(this.annotations).length
        },
    },
    methods: {
        genAnnotationCnf() {
            return {
                display: true,
                yMin: 0,
                yMax: 0,
                borderColor: '#EB5757',
                borderWidth: 1,
                label: {
                    backgroundColor: '#EB5757',
                    color: '#FFFFFF',
                    content: '',
                    display: true,
                    padding: 2,
                },
            }
        },
        onDelete(key) {
            this.$delete(this.annotations, key)
        },
        onAdd() {
            this.$set(this.annotations, this.$helpers.uuidv1(), this.genAnnotationCnf())
        },
    },
}
</script>

<style lang="scss" scoped>
.add-btn-block {
    height: 100px;
}
</style>

<template>
    <div class="annotations-cnf-ctr mt-2">
        <template v-for="(data, key) in annotations">
            <annotation-cnf
                v-if="!$typy(annotations, key).isEmptyObject"
                :key="key"
                v-model="annotations[key]"
                class="mb-4"
                @on-delete="onDelete(key)"
            />
        </template>
        <div
            v-ripple
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
import AnnotationCnf from '@rootSrc/pages/Dashboard/AnnotationCnf'

export default {
    name: 'annotations-cnf-ctr',
    components: { AnnotationCnf },
    props: {
        value: { type: Object, required: true },
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
    },
    methods: {
        genAnnotationCnf() {
            return {
                display: true,
                yMin: 80,
                yMax: 80,
                borderColor: 'rgba(235, 87, 87, 1)',
                borderWidth: 1,
                label: {
                    backgroundColor: 'rgba(235, 87, 87, 1)',
                    color: 'rgba(255, 255, 255, 1)',
                    content: 'max',
                    display: true,
                    padding: 2,
                },
            }
        },
        onDelete(key) {
            this.$delete(this.annotations, key)
        },
        onAdd() {
            const id = this.$helpers.uuidv1()
            this.$set(this.annotations, id, this.genAnnotationCnf())
        },
    },
}
</script>

<style lang="scss" scoped>
.add-btn-block {
    height: 100px;
}
</style>

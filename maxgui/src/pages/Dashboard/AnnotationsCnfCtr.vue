<template>
    <div class="annotations-cnf-ctr mt-2">
        <template v-for="(data, key) in annotations">
            <annotation-cnf
                v-if="!$typy(annotations, key).isEmptyObject"
                :key="key"
                v-model="annotations[key]"
                class="mb-4"
            />
        </template>
        <!-- TODO: Add button to add annotation and handle @on-delete event in annotation-cnf  -->
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
}
</script>

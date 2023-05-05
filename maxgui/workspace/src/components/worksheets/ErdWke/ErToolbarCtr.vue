<template>
    <div class="d-flex justify-start er-toolbar-ctr pt-1 px-3">
        <!-- TODO: Add buttons to change diagram options and button to generate ERD from existing db  -->
        <mxs-tooltip-btn
            btnClass="mr-2 toolbar-square-btn"
            :color="config.link.isAttrToAttr ? 'primary' : '#e8eef1'"
            @click="toggleIsAttrToAttr"
        >
            <template v-slot:btn-content>
                <v-icon size="22" :color="config.link.isAttrToAttr ? 'white' : 'blue-azure'">
                    mdi-key-link
                </v-icon>
            </template>
            {{ $mxs_t('info.drawFkLinks') }}
        </mxs-tooltip-btn>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'er-toolbar-ctr',
    props: {
        value: { type: Object, required: true },
    },
    computed: {
        config: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
    methods: {
        toggleIsAttrToAttr() {
            this.config = this.$helpers.immutableUpdate(this.config, {
                link: {
                    isAttrToAttr: { $set: !this.config.link.isAttrToAttr },
                },
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.er-toolbar-ctr {
    width: 100%;
    height: 32px;
    top: 0;
    z-index: 4;
}
</style>

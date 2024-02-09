<template>
    <div
        class="wke-toolbar d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border px-2"
    >
        <wke-toolbar-left-ctr @get-total-width="leftBtnsWidth = $event" />
        <v-spacer />
        <wke-toolbar-right @get-total-width="rightBtnsWidth = $event" />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('get-total-btn-width', v:number)
 */
import WkeToolbarLeftCtr from './WkeToolbarLeftCtr.vue'
import WkeToolbarRight from './WkeToolbarRight.vue'

export default {
    name: 'wke-toolbar',
    components: {
        WkeToolbarLeftCtr,
        WkeToolbarRight,
    },
    data() {
        return {
            queryConfigDialog: false,
            leftBtnsWidth: 0,
            rightBtnsWidth: 0,
        }
    },
    computed: {
        totalWidth() {
            return this.rightBtnsWidth + this.leftBtnsWidth
        },
    },
    watch: {
        totalWidth: {
            immediate: true,
            handler(v) {
                this.$emit('get-total-btn-width', v)
            },
        },
    },
}
</script>

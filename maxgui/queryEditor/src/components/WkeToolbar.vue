<template>
    <div
        class="wke-toolbar d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border px-2"
    >
        <wke-toolbar-left-btns-ctr @get-total-width="leftBtnsWidth = $event" />
        <v-spacer />
        <wke-toolbar-right-btns @get-total-width="rightBtnsWidth = $event" />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('get-total-btn-width', v:number)
 */
import WkeToolbarLeftBtns from './WkeToolbarLeftBtns.container.vue'
import WkeToolbarRightBtns from './WkeToolbarRightBtns.vue'

export default {
    name: 'wke-toolbar',
    components: {
        'wke-toolbar-left-btns-ctr': WkeToolbarLeftBtns,
        'wke-toolbar-right-btns': WkeToolbarRightBtns,
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

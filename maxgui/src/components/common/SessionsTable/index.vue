<template>
    <component
        :is="collapsible ? 'collapse' : 'div'"
        v-bind="
            collapsible
                ? {
                      toggleOnClick: () => (showTable = !showTable),
                      isContentVisible: showTable,
                      title: `${$tc('currentSessions', 2)}`,
                      titleInfo: rows.length,
                  }
                : null
        "
    >
        <data-table
            :data="rows"
            :loading="isLoading"
            :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
            :itemsPerPage="50"
            v-bind="{ ...$attrs }"
        >
            <!-- Pass on all scopedSlots of data-table -->
            <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                <slot :name="slot" v-bind="slotData" />
            </template>
        </data-table>
    </component>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState } from 'vuex'

export default {
    name: 'sessions-table',
    inheritAttrs: false, // bind $attrs to data-table
    props: {
        rows: { type: [Object, Array], required: true },
        collapsible: { type: Boolean, default: false },
        delayLoading: { type: Boolean, default: false },
    },
    data() {
        return {
            showTable: true,
            isMounting: true,
        }
    },
    computed: {
        ...mapState({ overlay_type: 'overlay_type' }),
        isLoading() {
            if (!this.delayLoading) return false
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
    },
    async mounted() {
        await this.$help.delay(this.delayLoading ? 400 : 0).then(() => (this.isMounting = false))
    },
}
</script>
<style lang="scss" scoped>
.info-table {
    td {
        white-space: nowrap;
        height: 24px;
        line-height: 1.5;
    }
}
</style>

<template>
    <component
        :is="collapsible ? 'mxs-collapse' : 'div'"
        v-bind="
            collapsible
                ? {
                      toggleOnClick: () => (showTable = !showTable),
                      isContentVisible: showTable,
                      title: `${$mxs_tc('currentSessions', 2)}`,
                      titleInfo: rows.length,
                  }
                : null
        "
    >
        <data-table
            :headers="headers"
            :data="rows"
            :loading="isLoading"
            :noDataText="$mxs_t('noEntity', { entityName: $mxs_tc('sessions', 2) })"
            showAll
            showActionsOnHover
            :customFilter="customFilter"
            :itemsPerPage="50"
            v-bind="{ ...$attrs }"
        >
            <!-- Pass on all scopedSlots of data-table -->
            <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                <slot :name="slot" v-bind="slotData" />
            </template>
            <template v-if="isAdmin" v-slot:actions="{ data: { item } }">
                <mxs-tooltip-btn icon @click="onKillSession(item)">
                    <template v-slot:btn-content>
                        <v-icon size="18" color="error">$vuetify.icons.mxs_unlink</v-icon>
                    </template>
                    {{ $mxs_t('killSession') }}
                </mxs-tooltip-btn>
            </template>
            <template
                v-if="headers.find(h => h.value === 'memory')"
                v-slot:memory="{ data: { item: { memory } } }"
            >
                <v-menu
                    top
                    offset-y
                    transition="slide-y-transition"
                    :close-on-content-click="false"
                    open-on-hover
                    allow-overflow
                    content-class="shadow-drop"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            v-mxs-highlighter="{
                                keyword: $typy($attrs, 'search').safeString,
                                txt: memory.total,
                            }"
                            class="pointer override-td--padding"
                            v-on="on"
                        >
                            {{ memory.total }}
                        </div>
                    </template>
                    <v-sheet class="py-4 px-3 text-body-2">
                        <table class="info-table px-1">
                            <tr v-for="(value, key) in memory" :key="key">
                                <td class="pr-5">{{ key }}</td>
                                <td
                                    v-mxs-highlighter="{
                                        keyword: $typy($attrs, 'search').safeString,
                                        txt: value,
                                    }"
                                >
                                    {{ value }}
                                </td>
                            </tr>
                        </table>
                    </v-sheet>
                </v-menu>
            </template>
        </data-table>
        <mxs-conf-dlg
            v-model="confDlg.isOpened"
            :title="$mxs_t('killSession')"
            saveText="kill"
            minBodyWidth="512px"
            :onSave="confirmKill"
        >
            <template v-if="confDlg.item" v-slot:body-prepend>
                <p class="confirmations-text" v-html="$mxs_t(`confirmations.killSession`)" />
                <table class="tbl-code pa-4">
                    <tr v-for="(v, key) in confDlg.item" :key="key">
                        <td>
                            <b>{{ key }}</b>
                        </td>
                        <td>{{ v }}</td>
                    </tr>
                </table>
            </template>
        </mxs-conf-dlg>
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
/*
@confirm-kill: item:object.
*/
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import { mapState, mapGetters } from 'vuex'
import asyncEmit from '@share/mixins/asyncEmit'

export default {
    name: 'sessions-table',
    mixins: [asyncEmit],
    inheritAttrs: false, // bind $attrs to data-table
    props: {
        rows: { type: [Object, Array], required: true },
        headers: { type: Array },
        collapsible: { type: Boolean, default: false },
        delayLoading: { type: Boolean, default: false },
    },
    data() {
        return {
            showTable: true,
            isMounting: true,
            confDlg: {
                isOpened: false,
            },
        }
    },
    computed: {
        ...mapState({ overlay_type: state => state.mxsApp.overlay_type }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        isLoading() {
            if (!this.delayLoading) return false
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
    },
    async mounted() {
        await this.$helpers.delay(this.delayLoading ? 400 : 0).then(() => (this.isMounting = false))
    },
    methods: {
        onKillSession(item) {
            this.confDlg = { isOpened: true, item }
        },
        async confirmKill() {
            await this.asyncEmit('confirm-kill', this.confDlg.item)
        },
        customFilter(v, search) {
            let value = `${v}`
            // filter for memory object
            if (this.$typy(v).isObject) value = JSON.stringify(v)
            return this.$helpers.ciStrIncludes(value, search)
        },
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

<template>
    <component
        :is="collapsible ? 'mxs-collapse' : 'div'"
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
            :headers="headers"
            :data="rows"
            :loading="isLoading"
            :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
            showAll
            showActionsOnHover
            v-bind="{ ...$attrs }"
        >
            <!-- Pass on all scopedSlots of data-table -->
            <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                <slot :name="slot" v-bind="slotData" />
            </template>
            <template v-if="isAdmin" v-slot:actions="{ data: { item } }">
                <v-tooltip
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <v-btn icon v-on="on" @click="onKillSession(item)">
                            <v-icon size="18" color="error">
                                $vuetify.icons.mxs_unlink
                            </v-icon>
                        </v-btn>
                    </template>
                    <span>{{ $t('killSession') }}</span>
                </v-tooltip>
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
                        <div class="pointer override-td--padding" v-on="on">
                            {{ memory.total }}
                        </div>
                    </template>
                    <v-sheet class="py-4 px-3 text-body-2">
                        <table class="info-table px-1">
                            <tr v-for="(value, key) in memory" :key="key">
                                <td class="pr-5">{{ key }}</td>
                                <td>{{ value }}</td>
                            </tr>
                        </table>
                    </v-sheet>
                </v-menu>
            </template>
        </data-table>
        <mxs-conf-dlg
            v-model="confDlg.isOpened"
            :title="$t('killSession')"
            type="kill"
            minBodyWidth="512px"
            :onSave="confirmKill"
        >
            <template v-if="confDlg.item" v-slot:body-prepend>
                <p class="confirmations-text" v-html="$t(`confirmations.killSession`)" />
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
 * Change Date: 2026-08-08
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
        ...mapState({ overlay_type: state => state.appNotifier.overlay_type }),
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

<template>
    <component
        :is="collapsible ? 'collapsible-ctr' : 'div'"
        v-bind="
            collapsible
                ? {
                      toggleOnClick: () => (showTable = !showTable),
                      isContentVisible: showTable,
                      title: `${$mxs_tc('currentSessions', 2)}`,
                      titleInfo: total,
                  }
                : null
        "
    >
        <mxs-data-table
            :loading="isLoading"
            :no-data-text="$mxs_t('noEntity', { entityName: $mxs_tc('sessions', 2) })"
            :options.sync="pagination"
            disable-sort
            :footer-props="footerOpts"
            :headers="headers"
            class="sessions-table"
            v-bind="{ ...$attrs }"
        >
            <!-- Pass on all scopedSlots of data-table -->
            <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                <slot :name="slot" v-bind="slotData" />
            </template>
            <template v-slot:[`item.memory`]="{ value: memory }">
                <memory-cell :data="memory" />
            </template>
            <template v-slot:[`item.action`]="slotData">
                <div
                    class="kill-session-btn"
                    :class="{
                        'kill-session-btn--visible': $helpers.lodash.isEqual(
                            confDlg.item,
                            slotData.item
                        ),
                    }"
                >
                    <mxs-tooltip-btn color="error" icon @click="onKillSession(slotData.item)">
                        <template v-slot:btn-content>
                            <v-icon size="18"> $vuetify.icons.mxs_unlink</v-icon>
                        </template>
                        {{ $mxs_t('killSession') }}
                    </mxs-tooltip-btn>
                </div>
            </template>
        </mxs-data-table>
        <confirm-dlg
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
        </confirm-dlg>
    </component>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * Emit:
 * @get-data-from-api: void
 * @confirm-kill: item:object.
 */
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import { mapState, mapMutations, mapGetters } from 'vuex'
import asyncEmit from '@share/mixins/asyncEmit'
import MemoryCell from '@src/components/SessionsTable/MemoryCell.vue'

export default {
    name: 'sessions-table',
    components: { MemoryCell },
    mixins: [asyncEmit],
    inheritAttrs: false, // bind $attrs to data-table
    props: {
        collapsible: { type: Boolean, default: false },
        delayLoading: { type: Boolean, default: false },
        extraHeaders: { type: Array, default: () => [] },
    },
    data() {
        return {
            showTable: true,
            isMounting: true,
            confDlg: {
                isOpened: false,
                item: null,
            },
        }
    },
    computed: {
        ...mapState({
            overlay_type: state => state.mxsApp.overlay_type,
            pagination_config: state => state.session.pagination_config,
        }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        headers() {
            return [
                { text: 'ID', value: 'id' },
                { text: 'Client', value: 'user' },
                { text: 'Connected', value: 'connected' },
                { text: 'IDLE (s)', value: 'idle' },
                { text: 'Memory', value: 'memory' },
                { text: 'I/O activity', value: 'io_activity' },
                ...this.extraHeaders,
                { text: '', value: 'action', sortable: false, width: '1px', cellClass: 'px-0' },
            ]
        },
        isLoading() {
            if (!this.delayLoading) return false
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        total() {
            return this.$attrs['server-items-length']
        },
        footerOpts() {
            return {
                'items-per-page-options': [5, 10, 50, 100, 500],
            }
        },
        // API page starts at 0, vuetify page starts at 1
        pagination: {
            get() {
                const page =
                    this.$typy(this.pagination_config, 'page').safeNumber === 0
                        ? 1
                        : this.pagination_config.page
                return { ...this.pagination_config, page }
            },
            set(v) {
                return this.SET_PAGINATION_CONFIG({ ...v, page: v.page - 1 })
            },
        },
    },
    watch: {
        pagination: {
            handler() {
                this.$emit('get-data-from-api')
            },
            deep: true,
        },

        'confDlg.isOpened'(v) {
            if (!v) this.confDlg.item = null
        },
    },
    created() {
        this.SET_DEF_PAGINATION_CONFIG()
    },
    async mounted() {
        await this.$helpers.delay(this.delayLoading ? 400 : 0).then(() => (this.isMounting = false))
    },
    methods: {
        ...mapMutations({
            SET_DEF_PAGINATION_CONFIG: 'session/SET_DEF_PAGINATION_CONFIG',
            SET_PAGINATION_CONFIG: 'session/SET_PAGINATION_CONFIG',
        }),
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
.sessions-table {
    tbody tr {
        .kill-session-btn {
            visibility: hidden;
            &--visible {
                visibility: visible;
            }
        }
    }
    tbody tr:hover {
        .kill-session-btn {
            visibility: visible;
        }
    }
}
</style>

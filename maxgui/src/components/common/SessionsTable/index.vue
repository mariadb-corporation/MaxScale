<template>
    <component
        :is="collapsible ? 'collapse' : 'div'"
        v-bind="
            collapsible
                ? {
                      toggleOnClick: () => (showTable = !showTable),
                      isContentVisible: showTable,
                      title: `${$tc('currentSessions', 2)}`,
                      titleInfo: total,
                  }
                : null
        "
    >
        <v-data-table
            class="session-table--std"
            :loading="isLoading"
            :no-data-text="$t('noEntity', { entityName: $tc('sessions', 2) })"
            :options.sync="pagination"
            disable-sort
            :footer-props="footerOpts"
            v-bind="{ ...$attrs }"
        >
            <!-- Pass on all scopedSlots of data-table -->
            <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                <slot :name="slot" v-bind="slotData" />
            </template>
        </v-data-table>
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * Emit:
 * @get-data-from-api: void
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'sessions-table',
    inheritAttrs: false, // bind $attrs to data-table
    props: {
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
        ...mapState({
            overlay_type: 'overlay_type',
            pagination_config: state => state.session.pagination_config,
        }),
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
    },
    created() {
        this.SET_DEF_PAGINATION_CONFIG()
    },
    async mounted() {
        await this.$help.delay(this.delayLoading ? 400 : 0).then(() => (this.isMounting = false))
    },
    methods: {
        ...mapMutations({
            SET_DEF_PAGINATION_CONFIG: 'session/SET_DEF_PAGINATION_CONFIG',
            SET_PAGINATION_CONFIG: 'session/SET_PAGINATION_CONFIG',
        }),
    },
}
</script>
<style lang="scss" scoped>
::v-deep .session-table--std {
    thead {
        tr {
            box-shadow: -7px 5px 7px -7px rgba(0, 0, 0, 0.1);
            th {
                background-color: $table-border !important;
                border-bottom: none !important;
                padding: 0 24px !important;
                color: $small-text !important;
                text-transform: uppercase;
                font-size: 11px;
                white-space: nowrap;
                &:first-child {
                    border-radius: 5px 0 0 0;
                }
                &:last-child {
                    border-radius: 0 5px 0 0;
                }
            }
        }
    }
    tbody {
        tr:hover {
            background: #fafcfc !important;
        }
        tr:active {
            background: #f2fcff !important;
        }
        td {
            padding: 0 24px !important;
            color: $navigation !important;
            border-bottom: thin solid $table-border !important;
            &:last-child:not(.hide) {
                border-right: thin solid $table-border;
            }
            &:first-child:not(.hide) {
                border-left: thin solid $table-border;
            }
        }
    }
    .v-data-footer {
        border-top: thin solid $table-border !important;
    }
}
</style>

<template>
    <div v-if="isVisualizingFilters" class="visualized-filters">
        <mxs-tooltip-btn
            btnClass="hide-filter-btn"
            x-small
            icon
            depressed
            @click="handleVisFilters"
        >
            <template v-slot:btn-content>
                <v-icon size="10" color="error"> $vuetify.icons.mxs_close</v-icon>
            </template>
            {{ $mxs_t('hideFilters') }}
        </mxs-tooltip-btn>
        <div class="filter-node-group pt-4">
            <div
                v-for="filter in filters.slice().reverse()"
                :key="filter.id"
                class="d-flex align-center flex-column"
            >
                <div
                    class="px-2 py-1 mx-auto filter-node d-flex justify-space-between"
                    :style="{ width: `${nodeWidth}px` }"
                >
                    <router-link
                        target="_blank"
                        rel="noopener noreferrer"
                        :to="`/dashboard/${filter.type}/${filter.id}`"
                        class="text-truncate mr-2"
                        :style="{ color: '#fff' }"
                    >
                        <mxs-truncate-str
                            :tooltipItem="{ txt: `${filter.id}` }"
                            :maxWidth="(nodeWidth - 30) / 2"
                        />
                    </router-link>
                    <mxs-truncate-str
                        :tooltipItem="{ txt: `${getFilterModule(filter.id)}` }"
                        :maxWidth="(nodeWidth - 30) / 2"
                    />
                </div>

                <div class="dashed-arrow d-flex justify-center">
                    <span class="line d-inline-block"></span>
                    <v-icon color="#f59d34" size="12" class="d-block arrow">
                        $vuetify.icons.mxs_arrowHead
                    </v-icon>
                </div>
            </div>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import { mapGetters } from 'vuex'
export default {
    name: 'filter-nodes',
    props: {
        value: { type: Boolean, required: true },
        filters: { type: Array, required: true },
        nodeWidth: { type: Number, required: true },
        handleVisFilters: { type: Function, required: true },
    },
    computed: {
        ...mapGetters({ getAllFiltersMap: 'filter/getAllFiltersMap' }),
        isVisualizingFilters: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
    },
    methods: {
        getFilterModule(id) {
            return this.$typy(this.getAllFiltersMap.get(id), 'attributes.module').safeString
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.visualized-filters {
    position: relative;
    .hide-filter-btn {
        position: absolute;
        right: 8px;
        top: 4px;
    }
    .filter-node-group {
        .filter-node {
            background-color: #f59d34;
            color: #fff;
            border: 1px solid #f59d34;
            border-radius: 4px;
        }
        .dashed-arrow {
            position: relative;
            height: 24px;
            .line {
                border-right: 2px dashed #f59d34;
                height: 22px;
            }
            .arrow {
                position: absolute;
                bottom: 0;
            }
        }
    }
}
</style>

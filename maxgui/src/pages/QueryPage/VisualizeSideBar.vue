<template>
    <div class="pa-4">
        <h5 class="mb-4">Visualization</h5>
        <label class="field__label color text-small-text"> Graph</label>
        <v-select
            v-model="selectedChart"
            :items="chartTypes"
            outlined
            class="std mariadb-select-input error--text__bottom"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            dense
            :height="36"
            hide-details="auto"
        />
        <div v-if="selectedChart !== 'No Visualization'" class="mt-2">
            <label class="field__label color text-small-text"> Select result set</label>
            <v-select
                v-model="selectedResultSet"
                :items="resultSets"
                outlined
                class="std mariadb-select-input error--text__bottom"
                :menu-props="{
                    contentClass: 'mariadb-select-v-menu',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                hide-details="auto"
                item-text="id"
                item-value="id"
                return-object
            />
        </div>
        <!-- TODO: add x, y inputs to select field -->
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
export default {
    name: 'visualize-sidebar',

    data() {
        return {
            selectedChart: 'No Visualization',
            chartTypes: ['No Visualization', 'Line', 'Bar - Horizontal', 'Bar - Vertical'],
            selectedResultSet: null,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            query_result: state => state.query.query_result,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
        }),
        resultSets() {
            let resSets = []

            let resSetArr = this.$typy(this.query_result, 'attributes.results').safeArray
            for (const [i, res] of resSetArr.entries()) {
                if (res.data) {
                    res.id = `RESULT SET ${i + 1}`
                    resSets.push(res)
                }
            }

            let prvwData = this.$typy(this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA))
                .safeObject
            if (!this.$typy(prvwData).isEmptyObject) {
                prvwData.id = this.$t('previewData')
                resSets.push(prvwData)
            }

            let prvwDataDetails = this.$typy(
                this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA_DETAILS)
            ).safeObject

            if (!this.$typy(prvwDataDetails).isEmptyObject) {
                prvwDataDetails.id = this.$t('viewDetails')
                resSets.push(prvwDataDetails)
            }

            return resSets
        },
    },
    watch: {
        selectedChart(v) {
            this.$emit('selected-chart', v)
        },
    },
}
</script>

<style lang="scss" scoped>
$label-size: 0.75rem;
.field__label {
    font-size: $label-size;
}
</style>

<template>
    <v-select
        ref="dateRange"
        v-model="selectedRange"
        :items="ranges"
        return-object
        outlined
        class="date-range-picker vuetify-input--override v-select--mariadb error--text__bottom"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
            left: true,
        }"
        dense
        :height="height"
        attach
        hide-details
    >
        <template v-slot:prepend-inner>
            <v-icon class="mr-1" size="16">$vuetify.icons.mxs_calendar </v-icon>
        </template>
        <!-- TODO: Use v-date-picker for custom date range-->
    </v-select>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { getUnixTime, subMonths, subDays, subWeeks, startOfDay, parseISO } from 'date-fns'

const TIME_REF_POINT_KEYS = [
    'NOW',
    'START_OF_TODAY',
    'START_OF_YESTERDAY',
    'NOW_MINUS_2_DAYS',
    'NOW_MINUS_LAST_WEEK',
    'NOW_MINUS_LAST_2_WEEKS',
    'NOW_MINUS_LAST_MONTH',
]

const TIME_REF_POINTS = TIME_REF_POINT_KEYS.reduce(
    (obj, key) => ({ ...obj, [key]: key.toLowerCase() }),
    {}
)

export default {
    name: 'date-range-picker',
    props: {
        value: { type: Array, required: true },
        height: { type: Number, default: 36 },
    },
    data() {
        return {
            selectedRange: {},
        }
    },
    computed: {
        range: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        ranges() {
            const {
                NOW,
                START_OF_TODAY,
                START_OF_YESTERDAY,
                NOW_MINUS_2_DAYS,
                NOW_MINUS_LAST_WEEK,
                NOW_MINUS_LAST_2_WEEKS,
                NOW_MINUS_LAST_MONTH,
            } = TIME_REF_POINTS
            return [
                {
                    text: this.$mxs_t('today'),
                    value: [START_OF_TODAY, NOW],
                },
                {
                    text: this.$mxs_t('yesterday'),
                    value: [START_OF_YESTERDAY, START_OF_TODAY],
                },
                {
                    text: this.$mxs_t('last2Days'),
                    value: [NOW_MINUS_2_DAYS, NOW],
                },
                {
                    text: this.$mxs_t('lastWeek'),
                    value: [NOW_MINUS_LAST_WEEK, NOW],
                },
                {
                    text: this.$mxs_t('last2Weeks'),
                    value: [NOW_MINUS_LAST_2_WEEKS, NOW],
                },
                {
                    text: this.$mxs_t('lastMonth'),
                    value: [NOW_MINUS_LAST_MONTH, NOW],
                },
            ]
        },
    },
    watch: {
        selectedRange(v) {
            const {
                value: [from, to],
            } = v
            this.range = [this.strToTimestamp(from), this.strToTimestamp(to)]
        },
    },
    created() {
        this.selectedRange = this.ranges[0]
    },
    methods: {
        /**
         * @param {string} - valid ISO date string or a value in TIME_REF_POINTS
         */
        strToTimestamp(v) {
            const {
                NOW,
                START_OF_TODAY,
                START_OF_YESTERDAY,
                NOW_MINUS_2_DAYS,
                NOW_MINUS_LAST_WEEK,
                NOW_MINUS_LAST_2_WEEKS,
                NOW_MINUS_LAST_MONTH,
            } = TIME_REF_POINTS
            switch (v) {
                case NOW:
                    return getUnixTime(new Date())
                case START_OF_TODAY:
                    return getUnixTime(startOfDay(new Date()))
                case START_OF_YESTERDAY:
                    return getUnixTime(startOfDay(subDays(new Date(), 1)))
                case NOW_MINUS_2_DAYS:
                    return getUnixTime(subDays(new Date(), 2))
                case NOW_MINUS_LAST_WEEK:
                    return getUnixTime(subWeeks(new Date(), 1))
                case NOW_MINUS_LAST_2_WEEKS:
                    return getUnixTime(subWeeks(new Date(), 2))
                case NOW_MINUS_LAST_MONTH:
                    return getUnixTime(subMonths(new Date(), 1))
                default:
                    // Assuming v is a valid ISO date string
                    return getUnixTime(parseISO(v))
            }
        },
    },
}
</script>

<style lang="scss">
.date-range-picker {
    max-width: 240px;
    .v-input__prepend-inner {
        margin-top: 0 !important;
        height: 100%;
        display: flex;
        align-items: center;
    }
}
</style>

<template>
    <v-select
        ref="dateRange"
        v-model="selectedRange"
        :items="ranges"
        return-object
        outlined
        class="vuetify-input--override v-select--mariadb error--text__bottom cursor--all-pointer"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb v-menu--mariadb-full-border',
            bottom: true,
            offsetY: true,
            left: true,
            nudgeTop: 1,
        }"
        dense
        :height="height"
        hide-details
        :style="{ maxWidth: `${width}px` }"
        @change="onSelectRange"
    >
        <template v-slot:prepend-inner>
            <v-icon class="mr-1 color--inherit" size="16">
                $vuetify.icons.mxs_calendar
            </v-icon>
        </template>
        <template v-slot:selection="{ item }">
            <template v-if="item.text === $mxs_t('customRange')">{{ customRangeTxt }}</template>
            <template v-else>{{ item.text }}</template>
        </template>
        <template v-slot:item="{ item }">
            <template v-if="item.text === $mxs_t('customRange')">
                <v-menu
                    v-model="menu"
                    :close-on-content-click="false"
                    :close-on-click="false"
                    nudge-bottom="64"
                    nudge-left="10"
                    bottom
                    internal-activator
                    content-class="v-menu--mariadb v-menu--mariadb-full-border"
                >
                    <template v-slot:activator="{ on, attrs }">
                        <div
                            v-bind="attrs"
                            class="d-flex flex-column flex-grow-1 cursor--all-pointer"
                            v-on="on"
                        >
                            <div :style="{ height: '28px' }" class="d-flex align-center">
                                {{ item.text }}
                            </div>
                            <v-text-field
                                :value="customRangeTxt"
                                class="vuetify-input--override"
                                readonly
                                outlined
                                dense
                                :height="36"
                                hide-details
                            >
                                <template v-slot:prepend-inner>
                                    <v-icon class="mr-1 color--inherit" size="16">
                                        $vuetify.icons.mxs_calendar
                                    </v-icon>
                                </template>
                            </v-text-field>
                        </div>
                    </template>
                    <v-date-picker v-model="item.value" color="primary" range no-title scrollable>
                        <v-spacer />
                        <v-btn
                            small
                            color="primary"
                            class="px-4 text-capitalize"
                            rounded
                            depressed
                            text
                            @click="menu = false"
                        >
                            {{ $mxs_t('cancel') }}
                        </v-btn>
                        <v-btn
                            small
                            color="primary"
                            class="px-7 text-capitalize"
                            rounded
                            depressed
                            :disabled="item.value.length <= 1"
                            @click="selectCustomRange(item.value)"
                        >
                            {{ $mxs_t('ok') }}
                        </v-btn>
                    </v-date-picker>
                </v-menu>
            </template>
            <template v-else>{{ item.text }}</template>
        </template>
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
/**
 * This component returns TIME_REF_POINTS or ISO date strings
 */
import { isToday, parseISO } from 'date-fns'
import { TIME_REF_POINTS } from '@src/constants'

const DEF_WIDTH = 160
const CUSTOM_RANGE_WIDTH = 250

export default {
    name: 'date-range-picker',
    props: {
        value: { type: Array, required: true },
        height: { type: Number, default: 36 },
    },
    data() {
        return {
            selectedRange: {},
            menu: false,
            ranges: [],
            width: DEF_WIDTH,
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
        customRangeTxt() {
            if (this.range.length === 2) {
                const { parseDateStr, dateFormat } = this.$helpers
                const formatType = 'yyyy-MM-dd'
                const [from, to] = this.range.map(v =>
                    dateFormat({ value: parseDateStr({ v }), formatType })
                )
                return `${from} to ${to}`
            }
            return ''
        },
    },
    created() {
        this.init()
    },
    methods: {
        init() {
            this.setRangeItems()
            this.selectedRange = this.ranges[0]
            this.onSelectRange(this.selectedRange)
        },
        setRangeItems() {
            const {
                NOW,
                START_OF_TODAY,
                END_OF_YESTERDAY,
                START_OF_YESTERDAY,
                NOW_MINUS_2_DAYS,
                NOW_MINUS_LAST_WEEK,
                NOW_MINUS_LAST_2_WEEKS,
                NOW_MINUS_LAST_MONTH,
            } = TIME_REF_POINTS
            this.ranges = [
                {
                    text: this.$mxs_t('today'),
                    value: [START_OF_TODAY, NOW],
                },
                {
                    text: this.$mxs_t('yesterday'),
                    value: [START_OF_YESTERDAY, END_OF_YESTERDAY],
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
                {
                    text: this.$mxs_t('customRange'),
                    value: [],
                },
            ]
        },
        /**
         * @param {array} values - ISO date strings or TIME_REF_POINTS
         */
        setRange(values) {
            this.range = values
        },
        closeSelect() {
            this.$refs.dateRange.blur()
        },
        onSelectRange(item) {
            this.setRange(item.value)
            this.width = DEF_WIDTH
        },
        selectCustomRange(values) {
            const { parseDateStr } = this.$helpers
            values.sort(
                (a, b) =>
                    parseDateStr({ v: a, toTimestamp: true }) -
                    parseDateStr({ v: b, toTimestamp: true })
            )
            const [from, to] = values
            /**
             * v-date-picker returns ISO date string so whenever `to` value is today,
             * parseDateStr returns the timestamp starts at the beginning of the day instead
             * of now
             */
            if (isToday(parseISO(to))) this.setRange([from, TIME_REF_POINTS.NOW])
            else this.setRange([from, to])

            this.selectedRange = this.ranges.find(item => item.text === this.$mxs_t('customRange'))
            this.closeSelect()
            this.width = CUSTOM_RANGE_WIDTH
        },
    },
}
</script>

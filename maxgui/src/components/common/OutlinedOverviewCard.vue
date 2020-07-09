<template>
    <div :class="['d-flex flex-column detail-overview', cardWrapper]">
        <p class="body-2 mb-3 text-uppercase color font-weight-bold text-navigation">
            <slot name="title">
                <span :style="{ visibility: 'hidden' }">hidden</span>
            </slot>
        </p>

        <v-card
            :tile="tile"
            class="d-flex align-center justify-center flex-column detail-overview__card"
            :class="[hover && 'pointer detail-overview__card--hover', ...cardClass]"
            height="75"
            outlined
            v-on="
                hoverableCard
                    ? {
                          mouseenter: e => hoverHandle(e),
                          mouseleave: e => hoverHandle(e),
                      }
                    : null
            "
        >
            <slot name="card-body"></slot>
        </v-card>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    props: {
        tile: { type: Boolean, default: true }, //Removes the component's border-radius.
        cardWrapper: String,
        cardStyle: Object,
        cardClass: String,
        hoverableCard: { type: Boolean, default: false },
    },
    data() {
        return {
            hover: false,
        }
    },
    methods: {
        hoverHandle(e) {
            this.hover = e.type === 'mouseenter'
            this.$emit('card-hover', this.hover)
        },
    },
}
</script>
<style lang="scss" scoped>
.detail-overview {
    width: 100%;

    &:not(:first-of-type) {
        .detail-overview__card {
            border-left: none !important;
        }
    }
}

.detail-overview__card--hover {
    background-color: #f2fcff;
}
</style>

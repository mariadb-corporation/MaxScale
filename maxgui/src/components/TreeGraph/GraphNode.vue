<template>
    <div ref="nodeWrapper" class="tree-node-wrapper d-flex flex-column fill-height">
        <v-card outlined class="node-card fill-height" :width="nodeWidth - 2">
            <slot name="node-heading"></slot>
            <v-divider />
            <div
                v-if="$slots['node-body'] || hasExtraInfo"
                :class="
                    `mxs-color-helper text-navigation d-flex justify-center flex-column px-3 py-1 ${bodyWrapperClass}`
                "
            >
                <slot name="node-body"></slot>
                <v-expand-transition>
                    <div
                        v-if="isExpanded && hasExtraInfo"
                        class="node-text--expanded-content mx-n3 mb-n2 px-3 pt-0 pb-2"
                    >
                        <v-carousel
                            v-model="activeInfoSlideIdx"
                            class="extra-info-carousel"
                            :show-arrows="false"
                            hide-delimiter-background
                            :height="maxNumOfExtraLines * lineHeightNum + carouselDelimiterHeight"
                        >
                            <v-carousel-item
                                v-for="(slide, i) in extraInfoSlides"
                                :key="i"
                                class="mt-4"
                            >
                                <div
                                    v-for="(value, key) in slide"
                                    :key="`${key}`"
                                    class="text-no-wrap d-flex"
                                    :style="{ lineHeight }"
                                >
                                    <span class="mr-2 font-weight-bold text-capitalize">
                                        {{ $mxs_t(key) }}
                                    </span>
                                    <mxs-truncate-str :tooltipItem="{ txt: `${value}` }" />
                                </div>
                            </v-carousel-item>
                        </v-carousel>
                    </div>
                </v-expand-transition>
            </div>
        </v-card>

        <v-btn
            v-if="hasExtraInfo"
            x-small
            height="16"
            class="arrow-toggle mx-auto px-2"
            style="box-sizing: content-box;"
            depressed
            outlined
            color="#e3e6ea"
            :style="{ backgroundColor: 'white' }"
            @click="toggleExpand(node)"
        >
            <v-icon :class="[isExpanded ? 'rotate-up' : 'rotate-down']" size="20" color="primary">
                mdi-chevron-down
            </v-icon>
        </v-btn>
    </div>
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
/* Expandable tree node
 * @node-height: v: Number. Node height
 * @get-expanded-node: v: String. Id of expanded node.
 */
export default {
    name: 'graph-node',
    props: {
        node: { type: Object, required: true },
        nodeWidth: { type: Number, default: 290 },
        lineHeight: { type: String, default: '18px' },
        bodyWrapperClass: { type: String, default: '' },
        expandOnMount: { type: Boolean, default: false },
        extraInfoSlides: { type: Array, default: () => [] },
    },
    data() {
        return {
            isExpanded: false,
            defHeight: 0,
            activeInfoSlideIdx: 0,
        }
    },
    computed: {
        hasExtraInfo() {
            return Boolean(this.extraInfoSlides.length)
        },
        carouselDelimiterHeight() {
            return 20
        },
        lineHeightNum() {
            return Number(this.lineHeight.replace('px', ''))
        },
        // Determine maximum number of new lines will be shown
        maxNumOfExtraLines() {
            let max = 0
            this.extraInfoSlides.forEach(slide => {
                let numOfLines = Object.keys(slide).length
                if (numOfLines > max) max = numOfLines
            })
            return max
        },
    },
    mounted() {
        this.$nextTick(() => {
            this.defHeight = this.$refs.nodeWrapper.clientHeight
            this.$emit('node-height', this.defHeight)
            if (this.expandOnMount && this.hasExtraInfo) this.toggleExpand(this.node)
        })
    },
    beforeDestroy() {
        this.$emit('get-expanded-node', { type: 'destroy', id: this.node.id })
    },
    methods: {
        getExpandedNodeHeight() {
            return (
                this.defHeight +
                this.maxNumOfExtraLines * this.lineHeightNum +
                this.carouselDelimiterHeight
            )
        },
        toggleExpand(node) {
            let height = this.defHeight
            this.isExpanded = !this.isExpanded
            // calculate the new height of the card before it's actually expanded
            if (this.isExpanded) {
                height = this.getExpandedNodeHeight()
            }
            this.$emit('get-expanded-node', { type: 'update', id: node.id })
            this.$emit('node-height', height)
        },
    },
}
</script>

<style lang="scss" scoped>
.tree-node-wrapper {
    background: transparent;
}
.node-card {
    font-size: 12px;
    .node-text--expanded-content {
        background: $separator;
        box-sizing: content-box;
        ::v-deep .extra-info-carousel {
            .v-carousel__controls {
                top: 0;
                height: 20px;
                .v-item-group {
                    height: 20px;
                    .v-carousel__controls__item {
                        background: white;
                        width: 32px;
                        height: 6px;
                        border-radius: 4px;
                        &::before {
                            // make it easier to click even the button height is visible as 6px, but it's actually 24px
                            top: -7px;
                            opacity: 0;
                            width: 32px;
                            height: 20px;
                            pointer-events: all;
                        }
                        i {
                            display: none;
                        }
                    }
                    .v-item--active {
                        background: $electric-ele;
                    }
                }
            }
        }
    }
}
</style>

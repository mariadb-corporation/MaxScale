<template>
    <div ref="nodeWrapper" class="cluster-node-wrapper d-flex flex-column fill-height">
        <v-card outlined class="node-card fill-height" width="288">
            <div
                class="node-title-wrapper d-flex align-center flex-row px-3 py-1"
                :class="[droppableTargets.includes(node.id) ? 'node-card__droppable' : '']"
            >
                <icon-sprite-sheet
                    size="13"
                    class="server-state-icon mr-1"
                    :frame="$help.serverStateIcon(nodeAttrs.state)"
                >
                    status
                </icon-sprite-sheet>
                <router-link
                    target="_blank"
                    :to="`/dashboard/servers/${node.id}`"
                    class="text-truncate rsrc-link"
                >
                    {{ node.id }}
                </router-link>
                <v-spacer />
                <span class="readonly-val ml-1 color text-field-text font-weight-medium">
                    {{ nodeAttrs.read_only ? $t('readonly') : $t('writable') }}
                </span>
                <div class="ml-1 button-container">
                    <v-menu
                        transition="slide-y-transition"
                        offset-y
                        nudge-left="100%"
                        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn small class="gear-btn" icon v-on="on">
                                <v-icon
                                    size="16"
                                    :color="
                                        droppableTargets.includes(node.id)
                                            ? 'background'
                                            : 'primary'
                                    "
                                >
                                    $vuetify.icons.settings
                                </v-icon>
                            </v-btn>
                        </template>
                        <!-- TODO: Render cluster node actions -->
                        <v-list class="color bg-color-background"> </v-list>
                    </v-menu>
                </div>
            </div>
            <v-divider />
            <div
                :class="
                    `color text-navigation d-flex justify-center flex-column px-3 py-1 ${nodeTxtWrapperClassName}`
                "
            >
                <div
                    class="node-state d-flex flex-row flex-grow-1 text-capitalize"
                    :style="{ lineHeight }"
                >
                    <span class="sbm mr-2 font-weight-bold">
                        {{ $t('state') }}
                    </span>
                    <truncate-string :text="`${nodeAttrs.state}`" />
                    <v-spacer />
                    <span v-if="!node.data.isMaster" class="sbm ml-1">
                        <span class="font-weight-bold text-capitalize">
                            {{ $t('lag') }}
                        </span>
                        <span> {{ sbm }}s </span>
                    </span>
                </div>
                <div class="node-connections d-flex flex-grow-1" :style="{ lineHeight }">
                    <span class="text-capitalize font-weight-bold mr-2">
                        {{ $tc('connections', 2) }}
                    </span>
                    <span>{{ nodeAttrs.statistics.connections }} </span>
                </div>
                <div class="node-address-socket d-flex flex-grow-1" :style="{ lineHeight }">
                    <span class="text-capitalize font-weight-bold mr-2">
                        {{ nodeAttrs.parameters.socket ? $t('socket') : $t('address') }}
                    </span>
                    <truncate-string
                        :text="
                            `${
                                nodeAttrs.parameters.socket
                                    ? nodeAttrs.parameters.socket
                                    : `${nodeAttrs.parameters.address}:${nodeAttrs.parameters.port}`
                            }`
                        "
                    />
                </div>
                <v-expand-transition>
                    <div
                        v-if="isExpanded"
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
                                    class="extra-info__line d-flex"
                                    :style="{ lineHeight }"
                                >
                                    <span class="mr-2 font-weight-bold">
                                        {{ $t(key) }}
                                    </span>
                                    <truncate-string :text="`${value}`" />
                                </div>
                            </v-carousel-item>
                        </v-carousel>
                    </div>
                </v-expand-transition>
            </div>
        </v-card>

        <v-btn
            v-if="Object.keys(activeInfoSlide).length"
            x-small
            height="16"
            class="arrow-toggle mx-auto text-capitalize font-weight-medium px-2 color bg-background"
            style="box-sizing: content-box;"
            depressed
            outlined
            color="#e3e6ea"
            @click="toggleExpand(node)"
        >
            <v-icon :class="[isExpanded ? 'arrow-up' : 'arrow-down']" size="20" color="primary">
                $expand
            </v-icon>
        </v-btn>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@cluster-node-height: v: Number. Cluster node height
@get-expanded-node: v: String. Id of expanded cluster node
*/
export default {
    name: 'cluster-node',
    props: {
        node: { type: Object, required: true },
        droppableTargets: { type: Array, required: true },
        nodeTxtWrapperClassName: { type: String, default: '' },
        expandOnMount: { type: Boolean, default: false },
    },
    data() {
        return {
            isExpanded: false,
            defHeight: 0,
            activeInfoSlideIdx: 0,
        }
    },
    computed: {
        carouselDelimiterHeight() {
            return 20
        },
        lineHeight() {
            return `18px`
        },
        lineHeightNum() {
            return Number(this.lineHeight.replace('px', ''))
        },
        nodeAttrs() {
            return this.node.data.serverData.attributes
        },
        // only slave node has this property
        slave_connections() {
            return this.$typy(this.node.data, 'server_info.slave_connections').safeArray
        },
        sbm() {
            return this.$help.getMin({
                arr: this.slave_connections,
                pickBy: 'seconds_behind_master',
            })
        },
        firstSlideCommonInfo() {
            return {
                last_event: this.nodeAttrs.last_event,
                gtid_binlog_pos: this.nodeAttrs.gtid_binlog_pos,
                gtid_current_pos: this.nodeAttrs.gtid_current_pos,
            }
        },
        secondSlideCommonInfo() {
            return {
                //TODO: calc uptime and replace triggered_at with it
                triggered_at: this.nodeAttrs.triggered_at,
                version_string: this.nodeAttrs.version_string,
            }
        },
        masterExtraInfo() {
            return [
                {
                    ...this.firstSlideCommonInfo,
                },
                this.secondSlideCommonInfo,
            ]
        },
        slaveExtraInfo() {
            return [
                {
                    ...this.firstSlideCommonInfo,
                    slave_io_running: this.$help.getMostFreq({
                        arr: this.slave_connections,
                        pickBy: 'slave_io_running',
                    }),
                    slave_sql_running: this.$help.getMostFreq({
                        arr: this.slave_connections,
                        pickBy: 'slave_sql_running',
                    }),
                },
                this.secondSlideCommonInfo,
            ]
        },
        extraInfo() {
            if (this.node.data.isMaster) return this.masterExtraInfo
            else return this.slaveExtraInfo
        },
        extraInfoSlides() {
            return this.extraInfo
        },
        activeInfoSlide() {
            return this.extraInfoSlides[this.activeInfoSlideIdx]
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
            this.$emit('cluster-node-height', this.defHeight)
            if (this.expandOnMount) this.toggleExpand(this.node)
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
            this.$emit('cluster-node-height', height)
        },
    },
}
</script>

<style lang="scss" scoped>
.cluster-node-wrapper {
    background: transparent;
}
.node-card {
    font-size: 12px;
    &__droppable {
        background: $success;
        color: $background;
        .rsrc-link {
            color: $background;
        }
        .readonly-val {
            color: $background !important;
        }
    }
    .node-text--expanded-content {
        background: $reflection;
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
                            background: white;
                            pointer-events: all;
                        }
                        i {
                            display: none;
                        }
                    }
                    .v-item--active {
                        background: $electric-ele;
                        &::before {
                            background: $electric-ele;
                        }
                    }
                }
            }
            .extra-info__line {
                white-space: nowrap;
            }
        }
    }
}
</style>

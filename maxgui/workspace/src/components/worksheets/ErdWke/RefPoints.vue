<template>
    <div class="ref-points">
        <!-- TODO: Add tooltip instruction -->
        <div
            v-for="(point, i) in points"
            :key="i"
            class="ref-point cursor-crosshair absolute"
            :style="{
                width: '10px',
                height: '10px',
                borderRadius: '50%',
                backgroundColor: node.styles.highlightColor,
                ...point.style,
            }"
            @mousedown.stop="dragStart({ e: $event, col: point.col })"
        />
        <!-- TODO: Add an svg here to drag FK line-->
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'ref-points',
    props: {
        node: { type: Object, required: true },
        entitySizeConfig: { type: Object, required: true },
        getColId: { type: Function, required: true },
        lookupNodes: { type: Array, required: true },
    },
    data() {
        return {
            defDraggingStates: {
                isDragging: false,
                srcAttrId: '',
                targetAttrId: '',
                startCoord: null,
                endCoord: null,
            },
            draggingStates: null,
        }
    },
    computed: {
        cols() {
            return this.node.data.definitions.cols
        },
        points() {
            return [...this.getPoints('l'), ...this.getPoints('r')]
        },
    },
    created() {
        this.setDefDraggingStates()
    },
    methods: {
        getTop(i) {
            const { headerHeight, rowHeight: k } = this.entitySizeConfig
            return `${headerHeight + (k * i + k) - k / 2 - 5}px`
        },
        getPoints(direction) {
            return this.node.data.definitions.cols.map((c, i) => ({
                style: {
                    left: `${(direction === 'r' ? this.node.size.width : 0) - 5}px`,
                    top: this.getTop(i),
                },
                col: c,
            }))
        },
        setDefDraggingStates() {
            this.draggingStates = this.$helpers.lodash.cloneDeep(this.defDraggingStates)
        },
        addDragEvents() {
            document.addEventListener('mousemove', this.drawing)
            document.addEventListener('mouseup', this.drawEnd)
        },
        rmDragEvents() {
            document.removeEventListener('mousemove', this.drawing)
            document.removeEventListener('mouseup', this.drawEnd)
        },
        dragStart({ e, col }) {
            document.body.classList.add('cursor--all-crosshair')
            this.draggingStates = {
                ...this.draggingStates,
                srcAttrId: this.getColId(col),
                startCoord: { x: e.clientX, y: e.clientY },
                endCoord: { x: e.clientX, y: e.clientY },
            }
            this.addDragEvents()
        },
        drawing(e) {
            e.preventDefault()
            e.stopPropagation()
            // update startCoord
            this.draggingStates = {
                ...this.draggingStates,
                isDragging: true,
                endCoord: { x: e.clientX, y: e.clientY },
            }
            this.$emit('drawing', { e, draggingStates: this.draggingStates })
        },
        drawEnd(e) {
            document.body.classList.remove('cursor--all-crosshair')
            //TODO: get targetAttrId in lookupNodes
            this.$emit('draw-end', { e, draggingStates: this.draggingStates })
            this.rmDragEvents()
            this.setDefDraggingStates()
        },
    },
}
</script>
